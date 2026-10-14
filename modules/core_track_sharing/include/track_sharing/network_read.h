#pragma once
#include "track_sharing/network_request.h"

namespace track_sharing
{
struct ReadQuery
{
    int32_t west = 0, south = 0, east = 0, north = 0;
    int8_t kind = -1, state = 0;
    geocaching::ByteView author;
    uint8_t limit = 16;
    bool has_snapshot = false, has_cursor = false;
    uint64_t snapshot = 0;
    geocaching::ByteView cursor_object, cursor_binding;
};
static_assert(sizeof(ReadQuery) <= 128, "Read query retains metadata only");

inline bool readSpan(geocaching::ByteView value, size_t bytes)
{
    return value.data && value.size == bytes;
}
inline bool readOptionalDisjoint(geocaching::ByteView value, uint8_t* output, size_t capacity)
{
    return !value.size || networkInputDisjoint(value, output, capacity);
}

// Caller provides output storage. Encoders own no buffers and cannot express
// uploads or publications. Query coordinates use canonical west/south/east/north.
inline bool encodeTrackReadQuery(geocaching::ByteView request, const ReadQuery& query, uint16_t budget,
                                 uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (query.west < -1800000000 || query.west > 1799999999 || query.east < -1800000000 || query.east > 1799999999 ||
        query.south < -900000000 || query.north > 900000000 || query.south > query.north ||
        query.kind < -1 || query.kind > 1 || query.state < -1 || query.state > 1 || !query.limit || query.limit > 32 ||
        (query.author.size && !readSpan(query.author, 64)) || query.snapshot > INT64_MAX ||
        (query.has_cursor && (!query.has_snapshot || !readSpan(query.cursor_object, 32) || !readSpan(query.cursor_binding, 32))) ||
        !networkInputDisjoint(request, output, capacity) ||
        !networkInputDisjoint({reinterpret_cast<const uint8_t*>(&query), sizeof(query)}, output, capacity) ||
        !readOptionalDisjoint(query.author, output, capacity) ||
        !readOptionalDisjoint(query.cursor_object, output, capacity) || !readOptionalDisjoint(query.cursor_binding, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!networkRequestHeader(writer, 1, request, budget, 7) || !writer.array(4) ||
        !writer.signedInteger(query.west) || !writer.signedInteger(query.south) ||
        !writer.signedInteger(query.east) || !writer.signedInteger(query.north) ||
        !(query.kind < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.kind))) ||
        !(query.author.size ? writer.binary(query.author) : writer.nil()) ||
        !(query.state < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.state))) || !writer.unsignedInteger(query.limit) ||
        !(query.has_snapshot ? writer.unsignedInteger(query.snapshot) : writer.nil())) return false;
    if (query.has_cursor)
    {
        if (!writer.array(2) || !writer.binary(query.cursor_object) || !writer.binary(query.cursor_binding)) return false;
    }
    else if (!writer.nil()) return false;
    written = writer.size();
    return writer.good();
}

inline bool encodeTrackReadManifest(geocaching::ByteView request, geocaching::ByteView object,
                                    geocaching::ByteView revision, uint16_t budget,
                                    uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!readSpan(object, 32) || (revision.size && !readSpan(revision, 32)) ||
        !networkInputDisjoint(request, output, capacity) || !networkInputDisjoint(object, output, capacity) ||
        !readOptionalDisjoint(revision, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!networkRequestHeader(writer, 2, request, budget, 2) || !writer.binary(object) ||
        !(revision.size ? writer.binary(revision) : writer.nil())) return false;
    written = writer.size();
    return writer.good();
}

inline bool encodeTrackReadFile(geocaching::ByteView request, geocaching::ByteView hash,
                                uint32_t offset, uint16_t length, uint16_t budget,
                                uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!readSpan(hash, 32) || !length || length > kNetworkChunkBytes || offset >= 8388608 || length > 8388608 - offset ||
        !networkInputDisjoint(request, output, capacity) || !networkInputDisjoint(hash, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!networkRequestHeader(writer, 8, request, budget, 3) || !writer.binary(hash) ||
        !writer.unsignedInteger(offset) || !writer.unsignedInteger(length)) return false;
    written = writer.size();
    return writer.good();
}
} // namespace track_sharing
