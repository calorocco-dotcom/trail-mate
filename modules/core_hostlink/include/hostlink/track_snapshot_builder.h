#pragma once

#include "hostlink/track_snapshot.h"

namespace hostlink::management
{
enum class SnapshotStorageResult : uint8_t
{
    Ok,
    Busy,
    Failed
};
enum class SnapshotBuildResult : uint8_t
{
    Pending,
    Ready,
    Invalid,
    Busy,
    Failed,
    Cancelled
};

// Storage owns source/destination handles and the streaming hash context.
// begin must acquire a closed-source lease: recording/replacement/removal and
// media transitions cannot mutate the source until finish/abort releases it.
// begin returning Busy must retain no lease or partially initialized resource.
// begin returning Failed may retain resources; abort must release them.
// Busy copy/finish leaves the operation retryable at the same offset.
// Busy copy must leave data/hash at the same offset. Failed copy may leave a
// partial candidate: the builder must abort it and never retry that chunk.
// finish verifies the lease, flushes/closes the immutable destination, stores
// its metadata atomically, and only then sets metadata.sealed. A short read,
// media change, or failed flush must never expose a sealed snapshot.
class TrackSnapshotStorage
{
  public:
    virtual ~TrackSnapshotStorage() = default;
    virtual SnapshotStorageResult begin(const uint8_t id[16], uint32_t bytes, uint8_t format) = 0;
    virtual SnapshotStorageResult copy(uint32_t offset, uint8_t* scratch, size_t length) = 0;
    virtual SnapshotStorageResult finish(TrackSnapshotMetadata& metadata) = 0;
    virtual SnapshotStorageResult abort() = 0;
    // Shutdown-only fallback: leave uncommitted files for later cleanup, but
    // release all live resources. Implementations unable to do so must refuse.
    virtual bool abandonForStop() { return false; }
};

// Owned by the existing storage scheduler, never an additional task/queue.
// No payload/file/hash buffer lives here. Each step copies at most 320 bytes
// using caller scratch; cancellation yields until storage releases its lease.
class TrackSnapshotBuilder
{
  public:
    explicit TrackSnapshotBuilder(TrackSnapshotStorage& storage) : storage_(storage) {}

    SnapshotBuildResult begin(const uint8_t id[16], uint32_t bytes, uint8_t format)
    {
        if (phase_ != Phase::Idle) return SnapshotBuildResult::Busy;
        uint8_t bits = 0;
        if (id)
            for (size_t i = 0; i < 16; ++i) bits |= id[i];
        if (!bits || !bytes || bytes > kTrackSnapshotMaxBytes || format > 3) return SnapshotBuildResult::Invalid;
        const auto result = storage_.begin(id, bytes, format);
        if (result == SnapshotStorageResult::Busy) return SnapshotBuildResult::Busy;
        if (result != SnapshotStorageResult::Ok)
        {
            // Initialization can fail after opening a file or acquiring a
            // lease. Retain ownership until the scheduler completes abort.
            offset_ = 0;
            phase_ = Phase::Failing;
            return SnapshotBuildResult::Pending;
        }
        std::memcpy(id_, id, 16);
        bytes_ = bytes;
        format_ = format;
        offset_ = 0;
        phase_ = Phase::Copying;
        return SnapshotBuildResult::Pending;
    }

    SnapshotBuildResult step(uint8_t* scratch, size_t capacity, TrackSnapshotMetadata& metadata)
    {
        metadata.sealed = false;
        if (phase_ == Phase::Idle) return SnapshotBuildResult::Invalid;
        if (phase_ == Phase::Cancelling || phase_ == Phase::Failing)
        {
            const auto result = storage_.abort();
            if (result == SnapshotStorageResult::Busy) return SnapshotBuildResult::Pending;
            // Failed abort retains ownership; do not permit another begin.
            if (result != SnapshotStorageResult::Ok) return SnapshotBuildResult::Failed;
            const bool cancelled = phase_ == Phase::Cancelling;
            phase_ = Phase::Idle;
            return cancelled ? SnapshotBuildResult::Cancelled : SnapshotBuildResult::Failed;
        }
        if (phase_ == Phase::Copying)
        {
            if (!scratch || !capacity) return SnapshotBuildResult::Invalid;
            const auto length = std::min<size_t>(std::min<size_t>(capacity, kTrackSnapshotChunkBytes), bytes_ - offset_);
            const auto result = storage_.copy(offset_, scratch, length);
            if (result == SnapshotStorageResult::Busy) return SnapshotBuildResult::Pending;
            if (result != SnapshotStorageResult::Ok)
            {
                phase_ = Phase::Failing;
                return SnapshotBuildResult::Pending;
            }
            offset_ += static_cast<uint32_t>(length);
            if (offset_ == bytes_) phase_ = Phase::Sealing;
            return SnapshotBuildResult::Pending;
        }
        const auto result = storage_.finish(metadata);
        if (result == SnapshotStorageResult::Busy)
        {
            metadata.sealed = false;
            return SnapshotBuildResult::Pending;
        }
        if (result != SnapshotStorageResult::Ok || !metadata.sealed || metadata.bytes != bytes_ ||
            metadata.format != format_ || std::memcmp(metadata.id, id_, 16))
        {
            metadata.sealed = false;
            phase_ = Phase::Failing;
            return SnapshotBuildResult::Pending;
        }
        phase_ = Phase::Idle;
        return SnapshotBuildResult::Ready;
    }

    bool abandonForStop()
    {
        if (phase_ == Phase::Idle) return true;
        if (!storage_.abandonForStop()) return false;
        phase_ = Phase::Idle;
        return true;
    }

    SnapshotBuildResult cancel()
    {
        if (phase_ == Phase::Idle) return SnapshotBuildResult::Cancelled;
        if (phase_ != Phase::Failing) phase_ = Phase::Cancelling;
        return SnapshotBuildResult::Pending;
    }
    uint32_t copiedBytes() const { return offset_; }
    bool active() const { return phase_ != Phase::Idle; }

  private:
    enum class Phase : uint8_t
    {
        Idle,
        Copying,
        Sealing,
        Cancelling,
        Failing
    };
    TrackSnapshotStorage& storage_;
    uint8_t id_[16]{};
    uint32_t bytes_ = 0, offset_ = 0;
    uint8_t format_ = 0;
    Phase phase_ = Phase::Idle;
};
static_assert(sizeof(TrackSnapshotBuilder) <= 40, "Snapshot builder must not contain payload storage");
} // namespace hostlink::management
