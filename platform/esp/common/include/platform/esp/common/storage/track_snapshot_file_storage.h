#pragma once

#include "hostlink/track_snapshot_builder.h"
#include "platform/esp/common/storage/record_file_io.h"
#include "platform/esp/common/storage/snapshot_container.h"

namespace platform::esp::storage
{
class SnapshotStreamHash
{
  public:
    virtual ~SnapshotStreamHash() = default;
    virtual bool start() = 0;
    virtual bool update(const uint8_t* bytes, size_t length) = 0;
    virtual bool finish(uint8_t hash[32]) = 0;
    virtual void reset() = 0;
};

// Paths belong to this lease and stay valid until release. acquire must reject
// live recordings and block source modification/removal. valid checks the
// captured media session as well as source ownership. Busy acquire owns nothing.
// acquire only obtains ownership; it must not create/truncate candidate files.
// Paths must be canonical and refer to distinct files (including aliases).
// commit publishes the closed single-file TSN1 container to an immutable
// destination. Data and metadata move together; no sidecar is needed. discard only removes this
// lease's uncommitted temporary files, never a previously committed snapshot.
class SnapshotFileLease
{
  public:
    virtual ~SnapshotFileLease() = default;
    virtual hostlink::management::SnapshotStorageResult acquire(const uint8_t id[16], uint32_t bytes, uint8_t format) = 0;
    virtual bool valid() const = 0;
    virtual const char* sourcePath() const = 0;
    virtual const char* temporaryPath() const = 0;
    virtual bool commit(const hostlink::management::TrackSnapshotMetadata& metadata) = 0;
    virtual bool discard() = 0;
    virtual void release() = 0;
};

// File handles are operation-scoped and never overlap: one reader closes
// before the writer opens. Hash context and scratch are caller-owned. Partial
// I/O is fatal to this candidate; the builder enters abort and never retries
// a chunk against a partially advanced stream/hash.
class TrackSnapshotFileStorage final : public hostlink::management::TrackSnapshotStorage
{
  public:
    TrackSnapshotFileStorage(RecordFileIo& io, SnapshotStreamHash& hash, SnapshotFileLease& lease)
        : io_(io), hash_(hash), lease_(lease) {}

    hostlink::management::SnapshotStorageResult begin(const uint8_t id[16], uint32_t bytes, uint8_t format) override
    {
        using Result = hostlink::management::SnapshotStorageResult;
        if (active_) return Result::Busy;
        uint8_t bits = 0;
        if (id)
            for (size_t i = 0; i < 16; ++i) bits |= id[i];
        if (!bits || !bytes || bytes > hostlink::management::kTrackSnapshotMaxBytes || format > 3) return Result::Failed;
        active_ = true;
        candidate_owned_ = false;
        const auto result = lease_.acquire(id, bytes, format);
        if (result == Result::Busy)
        {
            active_ = false;
            return result;
        }
        if (result != Result::Ok || !lease_.valid() || !lease_.sourcePath() || !*lease_.sourcePath() ||
            !lease_.temporaryPath() || !*lease_.temporaryPath() ||
            !std::strcmp(lease_.sourcePath(), lease_.temporaryPath()) || !sourceMatches(bytes)) return Result::Failed;
        candidate_owned_ = true;
        const auto file = io_.open(lease_.temporaryPath(), RecordFileIo::Mode::CreateTemporary);
        if (!file.handle) return Result::Failed;
        const bool initialized = io_.write(file.handle, kEmptySnapshotHeader, kSnapshotContainerHeaderBytes) == kSnapshotContainerHeaderBytes;
        const bool closed = io_.close(file.handle);
        if (!initialized || !closed || !hash_.start()) return Result::Failed;
        metadata_ = {};
        std::memcpy(metadata_.id, id, 16);
        metadata_.bytes = bytes;
        metadata_.format = format;
        offset_ = 0;
        return Result::Ok;
    }

    hostlink::management::SnapshotStorageResult copy(uint32_t offset, uint8_t* scratch, size_t length) override
    {
        using Result = hostlink::management::SnapshotStorageResult;
        if (!active_ || !lease_.valid() || offset != offset_ || !scratch || !length ||
            length > hostlink::management::kTrackSnapshotChunkBytes || offset > metadata_.bytes || length > metadata_.bytes - offset) return Result::Failed;
        const auto source = io_.open(lease_.sourcePath(), RecordFileIo::Mode::Read);
        if (!source.handle) return Result::Failed;
        uint64_t bytes = 0;
        bool ok = io_.size(source.handle, bytes) && bytes == metadata_.bytes && io_.seek(source.handle, offset) && io_.read(source.handle, scratch, length) == length;
        const bool source_closed = io_.close(source.handle);
        if (!ok || !source_closed || !lease_.valid()) return Result::Failed;
        const auto destination = io_.open(lease_.temporaryPath(), RecordFileIo::Mode::Update);
        if (!destination.handle) return Result::Failed;
        ok = io_.size(destination.handle, bytes) && bytes == kSnapshotContainerHeaderBytes + offset && io_.seek(destination.handle, kSnapshotContainerHeaderBytes + offset) && io_.write(destination.handle, scratch, length) == length;
        const bool destination_closed = io_.close(destination.handle);
        if (!ok || !destination_closed || !lease_.valid() || !hash_.update(scratch, length)) return Result::Failed;
        offset_ += static_cast<uint32_t>(length);
        return Result::Ok;
    }

    hostlink::management::SnapshotStorageResult finish(hostlink::management::TrackSnapshotMetadata& metadata) override
    {
        using Result = hostlink::management::SnapshotStorageResult;
        metadata.sealed = false;
        if (!active_ || offset_ != metadata_.bytes || !lease_.valid() || !sourceMatches(metadata_.bytes)) return Result::Failed;
        const auto file = io_.open(lease_.temporaryPath(), RecordFileIo::Mode::Update);
        if (!file.handle) return Result::Failed;
        uint64_t bytes = 0;
        const bool ok = io_.size(file.handle, bytes) && bytes == kSnapshotContainerHeaderBytes + metadata_.bytes &&
                        hash_.finish(metadata_.hash) && writeSnapshotContainerHeader(io_, file.handle, metadata_) && io_.sync(file.handle);
        const bool closed = io_.close(file.handle);
        if (!ok || !closed || !lease_.valid() || !lease_.commit(metadata_)) return Result::Failed;
        metadata = metadata_;
        metadata.sealed = true;
        hash_.reset();
        lease_.release();
        active_ = false;
        candidate_owned_ = false;
        return Result::Ok;
    }

    hostlink::management::SnapshotStorageResult abort() override
    {
        using Result = hostlink::management::SnapshotStorageResult;
        if (!active_) return Result::Ok;
        if (candidate_owned_ && !lease_.discard()) return Result::Failed;
        hash_.reset();
        lease_.release();
        active_ = false;
        candidate_owned_ = false;
        return Result::Ok;
    }

    bool abandonForStop() override
    {
        // The scheduler invokes this between slices, when all operation-scoped
        // handles are closed. Never publish or delete anything on this path.
        hash_.reset();
        lease_.release();
        active_ = false;
        candidate_owned_ = false;
        return true;
    }

  private:
    bool sourceMatches(uint32_t required)
    {
        const auto file = io_.open(lease_.sourcePath(), RecordFileIo::Mode::Read);
        if (!file.handle) return false;
        uint64_t bytes = 0;
        const bool matches = io_.size(file.handle, bytes) && bytes == required;
        return io_.close(file.handle) && matches;
    }
    RecordFileIo& io_;
    SnapshotStreamHash& hash_;
    SnapshotFileLease& lease_;
    hostlink::management::TrackSnapshotMetadata metadata_;
    uint32_t offset_ = 0;
    bool active_ = false, candidate_owned_ = false;
};
static_assert(sizeof(TrackSnapshotFileStorage) <= 104, "File snapshot adapter must not own payload/hash storage");
} // namespace platform::esp::storage
