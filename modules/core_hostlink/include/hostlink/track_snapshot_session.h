#pragma once

#include "hostlink/track_snapshot_builder.h"

namespace hostlink::management
{
enum class SnapshotSessionState : uint8_t
{
    Empty,
    Building,
    Cancelling,
    Ready,
    Failed,
    Cancelled
};

// Existing application scheduler lends scratch for each slice. This object
// owns only session/result metadata; storage, paths and hash have one owner.
// Never destroy that owner while releasable() is false: abort may need retries.
class SnapshotBuildSession
{
  public:
    explicit SnapshotBuildSession(TrackSnapshotBuilder& builder) : builder_(builder) {}

    SnapshotBuildResult begin(const uint8_t session[16], const uint8_t id[16],
                              uint32_t bytes, uint8_t format, uint64_t now)
    {
        if (!nonzero(session) || !nonzero(id)) return SnapshotBuildResult::Invalid;
        if (state_ != SnapshotSessionState::Empty) return SnapshotBuildResult::Busy;
        const auto result = builder_.begin(id, bytes, format);
        if (result != SnapshotBuildResult::Pending) return result;
        std::memcpy(session_, session, 16);
        std::memcpy(metadata_.id, id, 16);
        metadata_.bytes = bytes;
        metadata_.format = format;
        metadata_.sealed = false;
        last_touch_ = now;
        state_ = SnapshotSessionState::Building;
        return result;
    }

    // An unrelated request cannot extend this task's lifetime or cancel it.
    bool touch(const uint8_t session[16], const uint8_t id[16], uint64_t now)
    {
        if (!matches(session, id)) return false;
        last_touch_ = now;
        return true;
    }
    bool cancel(const uint8_t session[16], const uint8_t id[16])
    {
        if (!matches(session, id)) return false;
        if (state_ == SnapshotSessionState::Building)
        {
            builder_.cancel();
            state_ = SnapshotSessionState::Cancelling;
        }
        // Ready remains published: cancellation never removes sealed output.
        return true;
    }
    void advance(uint8_t* scratch, size_t capacity, uint64_t now, bool connected)
    {
        if (state_ != SnapshotSessionState::Building && state_ != SnapshotSessionState::Cancelling) return;
        if (!connected || now < last_touch_ || now - last_touch_ >= kIdleTimeoutMs)
        {
            builder_.cancel();
            state_ = SnapshotSessionState::Cancelling;
        }
        TrackSnapshotMetadata completed;
        const auto result = builder_.step(scratch, capacity, completed);
        if (result == SnapshotBuildResult::Ready)
        {
            metadata_ = completed;
            state_ = SnapshotSessionState::Ready;
        }
        else if (result == SnapshotBuildResult::Cancelled) state_ = SnapshotSessionState::Cancelled;
        else if (result == SnapshotBuildResult::Failed && !builder_.active()) state_ = SnapshotSessionState::Failed;
        // A failed abort retains the active builder and lease. Retry cleanup
        // on the next scheduler visit, never declare its memory releasable.
    }
    bool abandonForStop()
    {
        if (!builder_.abandonForStop()) return false;
        if (state_ == SnapshotSessionState::Building || state_ == SnapshotSessionState::Cancelling)
            state_ = SnapshotSessionState::Cancelled;
        return true;
    }
    bool matches(const uint8_t session[16], const uint8_t id[16]) const
    {
        return state_ != SnapshotSessionState::Empty && session && id &&
               !std::memcmp(session_, session, 16) && !std::memcmp(metadata_.id, id, 16);
    }
    SnapshotSessionState state() const { return state_; }
    const TrackSnapshotMetadata& metadata() const { return metadata_; }
    uint32_t copiedBytes() const { return builder_.copiedBytes(); }
    bool releasable() const { return !builder_.active(); }
    static constexpr uint64_t kIdleTimeoutMs = 30000;

  private:
    static bool nonzero(const uint8_t* value)
    {
        if (!value) return false;
        uint8_t bits = 0;
        for (unsigned i = 0; i < 16; ++i) bits |= value[i];
        return bits != 0;
    }
    TrackSnapshotBuilder& builder_;
    TrackSnapshotMetadata metadata_;
    uint8_t session_[16]{};
    uint64_t last_touch_ = 0;
    SnapshotSessionState state_ = SnapshotSessionState::Empty;
};
static_assert(sizeof(SnapshotBuildSession) <= 104, "Snapshot session must not own payload storage");
} // namespace hostlink::management
