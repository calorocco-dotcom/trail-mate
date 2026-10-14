#pragma once

#include "platform/esp/common/storage/snapshot_container.h"

namespace platform::esp::storage
{
enum class SnapshotReadResult : uint8_t
{
    Ok,
    Missing,
    InvalidRequest,
    Conflict,
    Capacity,
    StorageFailure
};

// The platform resolves id to an immutable device-owned container path and
// its RecordFileIo must check the captured media session. Never accept a path
// supplied directly by the browser. Metadata/request cannot alias output.
// File bytes go directly into the existing response payload after its prefix.
inline SnapshotReadResult readSnapshotFileResponse(RecordFileIo& io, const char* path,
                                                   const hostlink::management::TrackSnapshotRequest& request,
                                                   hostlink::management::TrackSnapshotMetadata& metadata,
                                                   uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!path || !*path || !output || !request.maximum || request.maximum > hostlink::management::kTrackSnapshotChunkBytes ||
        (request.offset && !request.has_hash)) return SnapshotReadResult::InvalidRequest;
    const auto opened = io.open(path, RecordFileIo::Mode::Read);
    if (!opened.handle) return opened.status == RecordFileIo::OpenStatus::Missing ? SnapshotReadResult::Missing : SnapshotReadResult::StorageFailure;
    SnapshotReadResult result = SnapshotReadResult::StorageFailure;
    size_t chunk_offset = 0, chunk_length = 0, encoded = 0;
    if (readSnapshotContainerHeader(io, opened.handle, metadata))
    {
        if (std::memcmp(request.id, metadata.id, 16) || (request.has_hash && std::memcmp(request.expected_hash, metadata.hash, 32))) result = SnapshotReadResult::Conflict;
        else if (request.offset >= metadata.bytes) result = SnapshotReadResult::InvalidRequest;
        else if (!hostlink::management::prepareTrackSnapshotResponse(request, metadata, output, capacity, chunk_offset, chunk_length, encoded)) result = SnapshotReadResult::Capacity;
        else if (io.seek(opened.handle, kSnapshotContainerHeaderBytes + request.offset) && io.read(opened.handle, output + chunk_offset, chunk_length) == chunk_length) result = SnapshotReadResult::Ok;
    }
    if (!io.close(opened.handle)) result = SnapshotReadResult::StorageFailure;
    if (result == SnapshotReadResult::Ok) written = encoded;
    return result;
}
} // namespace platform::esp::storage
