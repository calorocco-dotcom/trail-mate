#pragma once

#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include <algorithm>
#include <cstring>

namespace hostlink::management
{
constexpr uint32_t kTrackSnapshotMaxBytes = 8U * 1024U * 1024U;
constexpr uint16_t kTrackSnapshotChunkBytes = 320;

struct TrackSnapshotRequest
{
    uint8_t id[16]{};
    uint8_t expected_hash[32]{};
    uint32_t offset = 0;
    uint16_t maximum = 0;
    bool has_hash = false;
};
struct TrackSnapshotMetadata
{
    uint8_t id[16]{};
    uint8_t hash[32]{};
    uint32_t bytes = 0;
    uint8_t format = 0;
    // Set only by storage after a completed immutable snapshot and hash.
    bool sealed = false;
};
static_assert(sizeof(TrackSnapshotRequest) <= 56, "Snapshot request must not contain file storage");
static_assert(sizeof(TrackSnapshotMetadata) <= 56, "Snapshot metadata must remain bounded");

inline bool decodeTrackSnapshotRequest(geocaching::ByteView body, TrackSnapshotRequest& output)
{
    output = {};
    geocaching::protocol::CmpReader reader(body);
    size_t fields = 0;
    uint64_t offset = 0, maximum = 0;
    geocaching::ByteView id, hash;
    if (!reader.array(fields, 4) || fields != 4 || !reader.binary(id, 16) || id.size != 16 ||
        !reader.unsignedInteger(offset) || offset >= kTrackSnapshotMaxBytes ||
        !reader.unsignedInteger(maximum) || !maximum || maximum > kTrackSnapshotChunkBytes) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < id.size; ++i) bits |= id.data[i];
    if (!bits) return false;
    const bool nil = reader.position() < body.size && body.data[reader.position()] == 0xc0;
    if (nil ? !reader.nil() : (!reader.binary(hash, 32) || hash.size != 32)) return false;
    if (!reader.finished() || (offset && nil)) return false;
    std::memcpy(output.id, id.data, 16);
    if (!nil) std::memcpy(output.expected_hash, hash.data, 32);
    output.offset = static_cast<uint32_t>(offset);
    output.maximum = static_cast<uint16_t>(maximum);
    output.has_hash = !nil;
    return true;
}

// Header/trailer go into the existing response payload. The storage adapter
// reads exactly chunk_length bytes directly into output + chunk_offset.
// A failed/short read must discard the whole response; no success is emitted.
// Request/metadata must not alias output. This function does not hash or seal.
inline bool prepareTrackSnapshotResponse(const TrackSnapshotRequest& request,
                                         const TrackSnapshotMetadata& metadata,
                                         uint8_t* output, size_t capacity,
                                         size_t& chunk_offset, size_t& chunk_length, size_t& written)
{
    chunk_offset = chunk_length = written = 0;
    if (!metadata.sealed || !metadata.bytes || metadata.bytes > kTrackSnapshotMaxBytes || metadata.format > 3 ||
        !request.maximum || request.maximum > kTrackSnapshotChunkBytes || request.offset >= metadata.bytes ||
        std::memcmp(request.id, metadata.id, 16) ||
        (request.offset && !request.has_hash) ||
        (request.has_hash && std::memcmp(request.expected_hash, metadata.hash, 32))) return false;
    uint8_t bits = 0;
    for (auto byte : metadata.id) bits |= byte;
    if (!bits) return false;
    const auto length = std::min<uint32_t>(request.maximum, metadata.bytes - request.offset);
    geocaching::protocol::CmpWriter prefix(output, capacity);
    if (!prefix.array(6) || !prefix.binary({metadata.id, 16}) || !prefix.unsignedInteger(metadata.bytes) ||
        !prefix.binary({metadata.hash, 32}) || !prefix.unsignedInteger(metadata.format) || !prefix.binaryHeader(length)) return false;
    const auto start = prefix.size();
    if (start > capacity || length > capacity - start) return false;
    geocaching::protocol::CmpWriter suffix(output + start + length, capacity - start - length);
    if (!suffix.unsignedInteger(request.offset)) return false;
    chunk_offset = start;
    chunk_length = length;
    written = start + length + suffix.size();
    return true;
}
} // namespace hostlink::management
