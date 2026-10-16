#pragma once
#include "geocaching/protocol/cmp_writer.h"

namespace track_sharing
{
inline constexpr char kApplicationType[] = "trailmate.track.v1";
inline constexpr size_t kNetworkChunkBytes = 1024;

// Caller owns output and input leases. No allocation, identity material, or
// retained KML buffer. A successful encoding does not admit a publication.
inline bool networkRequestId(geocaching::ByteView id)
{
    if (!id.data || id.size != 16) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < id.size; ++i) bits |= id.data[i];
    return bits != 0;
}

inline bool networkInputDisjoint(geocaching::ByteView input, uint8_t* output, size_t capacity)
{
    if (!input.data || !output || !capacity) return false;
    const auto source = reinterpret_cast<uintptr_t>(input.data);
    const auto target = reinterpret_cast<uintptr_t>(output);
    return source <= target ? target - source >= input.size : source - target >= capacity;
}

inline bool networkRequestHeader(geocaching::protocol::CmpWriter& writer,
                                 uint8_t operation, geocaching::ByteView request,
                                 uint16_t budget, uint32_t fields)
{
    return operation <= 8 && networkRequestId(request) && budget >= 512 && budget <= 8192 &&
           writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(0) &&
           writer.unsignedInteger(operation) && writer.binary(request) &&
           writer.unsignedInteger(budget) && writer.array(fields);
}

inline bool encodeTrackUploadBegin(geocaching::ByteView request, geocaching::ByteView hash,
                                   uint32_t bytes, uint16_t budget, uint8_t* output,
                                   size_t capacity, size_t& written)
{
    written = 0;
    if (!hash.data || hash.size != 32 || !bytes || bytes > 8388608 ||
        !networkInputDisjoint(request, output, capacity) || !networkInputDisjoint(hash, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!networkRequestHeader(writer, 4, request, budget, 2) || !writer.binary(hash) || !writer.unsignedInteger(bytes)) return false;
    written = writer.size();
    return writer.good();
}

inline bool encodeTrackUploadChunk(geocaching::ByteView request, geocaching::ByteView token,
                                   uint32_t offset, geocaching::ByteView chunk, uint16_t budget,
                                   uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!networkRequestId(token) || !chunk.data || !chunk.size || chunk.size > kNetworkChunkBytes ||
        offset > 8388608 || chunk.size > 8388608 - offset ||
        !networkInputDisjoint(request, output, capacity) || !networkInputDisjoint(token, output, capacity) ||
        !networkInputDisjoint(chunk, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!networkRequestHeader(writer, 5, request, budget, 3) || !writer.binary(token) ||
        !writer.unsignedInteger(offset) || !writer.binary(chunk)) return false;
    written = writer.size();
    return writer.good();
}

inline bool encodeTrackUploadFinish(geocaching::ByteView request, geocaching::ByteView token,
                                    uint16_t budget, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!networkRequestId(token) || !networkInputDisjoint(request, output, capacity) ||
        !networkInputDisjoint(token, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!networkRequestHeader(writer, 6, request, budget, 1) || !writer.binary(token)) return false;
    written = writer.size();
    return writer.good();
}
} // namespace track_sharing
