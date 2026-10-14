#pragma once

#include "hostlink/management_envelope.h"
#include "platform/esp/common/storage/snapshot_file_response.h"

namespace hostlink::management
{
// Read only sealed, device-owned containers. The source recording pathname is
// never supplied by a client. Scratch reuses the response tail and cannot
// overlap the bounded 320-byte chunk response. The caller owns the file I/O.
inline Status readTrackSnapshotService(const EnvelopeView& envelope,
                                       platform::esp::storage::RecordFileIo& io,
                                       uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    TrackSnapshotRequest request;
    if (!decodeTrackSnapshotRequest({envelope.body, envelope.body_size}, request)) return Status::InvalidField;
    if (!output || capacity < 464) return Status::Internal;
    char* path = reinterpret_cast<char*>(output + capacity - 64);
    constexpr char prefix[] = "/trailmate/web-tracks/";
    constexpr char hex[] = "0123456789abcdef";
    std::memcpy(path, prefix, sizeof(prefix) - 1);
    size_t position = sizeof(prefix) - 1;
    for (const auto byte : request.id)
    {
        path[position++] = hex[byte >> 4];
        path[position++] = hex[byte & 15];
    }
    std::memcpy(path + position, ".tsn", 5);
    TrackSnapshotMetadata metadata;
    using platform::esp::storage::SnapshotReadResult;
    switch (platform::esp::storage::readSnapshotFileResponse(io, path, request, metadata, output, capacity - 64, written))
    {
    case SnapshotReadResult::Ok:
        return Status::Ok;
    case SnapshotReadResult::Missing:
        return Status::Expired;
    case SnapshotReadResult::InvalidRequest:
        return Status::InvalidField;
    case SnapshotReadResult::Conflict:
        return Status::Conflict;
    case SnapshotReadResult::Capacity:
        return Status::Internal;
    default:
        return Status::StorageUnavailable;
    }
}
} // namespace hostlink::management
