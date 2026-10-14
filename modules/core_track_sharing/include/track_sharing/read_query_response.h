#pragma once
#include "geocaching/protocol/record_decoder.h"
#include "track_sharing/network_read.h"
#include "track_sharing/network_upload_response.h"

namespace track_sharing
{
struct ReadQueryRow
{
    geocaching::ByteView object, author;
    int32_t west = 0, south = 0, east = 0, north = 0;
    uint8_t kind = 0, state = 0;
};
struct ReadQueryResponse
{
    geocaching::ByteView cursor_object, cursor_binding;
    uint64_t snapshot = 0;
    uint8_t rows = 0;
    bool has_cursor = false;
};
static_assert(sizeof(ReadQueryRow) <= 64 && sizeof(ReadQueryResponse) <= 56, "Query views must not retain a page");

inline bool readQueryIntersects(const ReadQuery& query, const ReadQueryRow& row)
{
    if (query.south > row.north || row.south > query.north) return false;
    if (query.west > query.east && row.west > row.east) return true;
    if (query.west > query.east) return query.west <= row.east || row.west <= query.east;
    if (row.west > row.east) return row.west <= query.east || query.west <= row.east;
    return query.west <= row.east && row.west <= query.east;
}
inline bool decodeReadQueryRow(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw, ReadQueryRow& row)
{
    row = {};
    size_t count = 0;
    uint64_t value = 0, revision = 0;
    int64_t coordinate = 0;
    geocaching::ByteView version;
    std::string_view title;
    if (!reader.array(count, 10) || count != 10 || !reader.binary(row.object, 32) || row.object.size != 32 ||
        !reader.binary(version, 32) || version.size != 32 || !reader.unsignedInteger(revision) || !revision || revision > UINT32_MAX ||
        !reader.unsignedInteger(value) || value > 1) return false;
    row.kind = uint8_t(value);
    if (!reader.unsignedInteger(value) || value > 1 || (revision == 1 && value)) return false;
    row.state = uint8_t(value);
    if (!reader.text(title, 128) || !geocaching::protocol::validRecordText(title, false, true) || !reader.array(count, 4) || count != 4) return false;
    const auto angle = [&](int64_t low, int64_t high, int32_t& target)
    {if(!reader.signedInteger(coordinate) || coordinate<low || coordinate>high)return false;target=int32_t(coordinate);return true; };
    if (!angle(-1800000000, 1799999999, row.west) || !angle(-900000000, 900000000, row.south) ||
        !angle(-1800000000, 1799999999, row.east) || !angle(row.south, 900000000, row.north) ||
        !reader.array(count, 4) || count != 4) return false;
    for (size_t i = 0; i < 4; ++i)
    {
        if ((i == 1 || i == 2) && reader.position() < raw.size && raw.data[reader.position()] == 0xc0)
        {
            if (!reader.nil()) return false;
        }
        else if (!reader.unsignedInteger(value) || value > (i == 3 ? 100000 : UINT32_MAX) || (i == 3 && !value)) return false;
    }
    if (!reader.unsignedInteger(value) || !value || value > 8388608 || !reader.binary(row.author, 64) || row.author.size != 64) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < 64; ++i) bits |= row.author.data[i];
    return bits != 0;
}

// Owner lends one row scratch and output. All returned views are callback-local.
// For operation 1, binding is SHA-256(CMP1([box,kind,author,state])). For 13,
// caller derives it from the immutable region ID/version/relation/filter scope.
inline bool decodeTrackReadQueryResponse(geocaching::ByteView raw, geocaching::ByteView request,
                                         const ReadQuery& query, geocaching::ByteView expected_binding,
                                         ReadQueryRow& row, ReadQueryResponse& output, uint8_t operation = 1)
{
    if ((operation != 1 && operation != 13) || !raw.data || !raw.size || raw.size > 8192 || !readSpan(expected_binding, 32) || !query.limit || query.limit > 32 ||
        query.west < -1800000000 || query.west > 1799999999 || query.east < -1800000000 || query.east > 1799999999 ||
        query.south < -900000000 || query.north > 900000000 || query.south > query.north ||
        query.kind < -1 || query.kind > 1 || query.state < -1 || query.state > 1 || query.snapshot > INT64_MAX ||
        (query.author.size && !readSpan(query.author, 64)) ||
        (query.has_cursor && (!query.has_snapshot || !readSpan(query.cursor_object, 32) || !readSpan(query.cursor_binding, 32) || std::memcmp(query.cursor_binding.data, expected_binding.data, 32)))) return false;
    for (const auto input : {raw, request, expected_binding, query.author, query.cursor_object, query.cursor_binding,
                             geocaching::ByteView{reinterpret_cast<const uint8_t*>(&query), sizeof(query)}})
    {
        if (input.size && (!networkInputDisjoint(input, reinterpret_cast<uint8_t*>(&row), sizeof(row)) ||
                           !networkInputDisjoint(input, reinterpret_cast<uint8_t*>(&output), sizeof(output)))) return false;
    }
    if (!networkInputDisjoint({reinterpret_cast<const uint8_t*>(&row), sizeof(row)}, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader(raw);
    size_t count = 0;
    uint64_t snapshot = 0;
    geocaching::ByteView cursor_object, cursor_binding;
    bool has_cursor = false;
    if (!uploadResponseHeader(reader, operation, request, 3) || !reader.unsignedInteger(snapshot) || snapshot > INT64_MAX ||
        (query.has_snapshot && snapshot != query.snapshot)) return false;
    if (reader.position() < raw.size && raw.data[reader.position()] == 0xc0)
    {
        if (!reader.nil()) return false;
    }
    else
    {
        if (!reader.array(count, 2) || count != 2 || !reader.binary(cursor_object, 32) || cursor_object.size != 32 ||
            !reader.binary(cursor_binding, 32) || cursor_binding.size != 32 || std::memcmp(cursor_binding.data, expected_binding.data, 32) ||
            (query.has_cursor && std::memcmp(cursor_object.data, query.cursor_object.data, 32) <= 0)) return false;
        has_cursor = true;
    }
    if (!reader.array(count, query.limit)) return false;
    geocaching::ByteView previous = query.has_cursor ? query.cursor_object : geocaching::ByteView{};
    for (size_t i = 0; i < count; ++i)
    {
        if (!decodeReadQueryRow(reader, raw, row) || (previous.size && std::memcmp(row.object.data, previous.data, 32) <= 0) ||
            (query.kind >= 0 && row.kind != query.kind) || (query.state >= 0 && row.state != query.state) ||
            (query.author.size && std::memcmp(query.author.data, row.author.data, 64)) || !readQueryIntersects(query, row)) return false;
        previous = row.object;
    }
    if (!reader.finished() || (has_cursor && previous.size && std::memcmp(cursor_object.data, previous.data, 32) < 0)) return false;
    output = {cursor_object, cursor_binding, snapshot, uint8_t(count), has_cursor};
    return true;
}
} // namespace track_sharing
