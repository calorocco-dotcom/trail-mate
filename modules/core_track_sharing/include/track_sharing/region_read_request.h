#pragma once
#include "track_sharing/network_read.h"
#include <string_view>

namespace track_sharing
{
inline bool regionId(geocaching::ByteView value)
{
    if (!value.data || value.size < 3 || value.size > 89) return false;
    size_t colon = 0;
    for (; colon < value.size && value.data[colon] != ':'; ++colon)
    {
        const auto c = value.data[colon];
        if (colon ? !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-') : !(c >= 'a' && c <= 'z')) return false;
    }
    if (!colon || colon > 24 || colon + 1 >= value.size || value.size - colon - 1 > 64) return false;
    for (size_t i = colon + 1; i < value.size; ++i)
    {
        const auto c = value.data[i];
        const bool alphanumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alphanumeric && (i == colon + 1 || (c != '.' && c != '_' && c != '-'))) return false;
    }
    return true;
}
inline bool regionOptionalId(geocaching::ByteView value) { return !value.size || regionId(value); }
inline bool regionVersion(geocaching::ByteView value)
{
    if (!readSpan(value, 32)) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < value.size; ++i) bits |= value.data[i];
    return bits != 0;
}
inline std::string_view regionText(geocaching::ByteView value) { return {reinterpret_cast<const char*>(value.data), value.size}; }
inline bool regionReadHeader(geocaching::protocol::CmpWriter& writer, uint8_t operation, geocaching::ByteView request, uint16_t budget, uint8_t fields)
{
    return operation >= 9 && operation <= 13 && networkRequestId(request) && budget >= 512 && budget <= 8192 &&
           writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(0) && writer.unsignedInteger(operation) &&
           writer.binary(request) && writer.unsignedInteger(budget) && writer.array(fields);
}
inline bool regionInputs(geocaching::ByteView request, geocaching::ByteView version, geocaching::ByteView id,
                         uint8_t* output, size_t capacity)
{
    return regionVersion(version) && regionOptionalId(id) && networkInputDisjoint(request, output, capacity) &&
           networkInputDisjoint(version, output, capacity) && readOptionalDisjoint(id, output, capacity);
}
inline bool encodeRegionCapabilities(geocaching::ByteView request, uint16_t budget, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!networkInputDisjoint(request, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!regionReadHeader(writer, 9, request, budget, 0)) return false;
    written = writer.size();
    return writer.good();
}
inline bool encodeRegionChildren(geocaching::ByteView request, geocaching::ByteView version, geocaching::ByteView parent,
                                 geocaching::ByteView after, uint8_t limit, uint16_t budget, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!regionInputs(request, version, parent, output, capacity) || !regionOptionalId(after) || !readOptionalDisjoint(after, output, capacity) || !limit || limit > 32) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!regionReadHeader(writer, 10, request, budget, 4) || !writer.binary(version) ||
        !(parent.size ? writer.text(regionText(parent)) : writer.nil()) || !(after.size ? writer.text(regionText(after)) : writer.nil()) || !writer.unsignedInteger(limit)) return false;
    written = writer.size();
    return writer.good();
}
inline bool encodeRegionDescriptor(geocaching::ByteView request, geocaching::ByteView id, geocaching::ByteView version,
                                   uint16_t budget, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!regionId(id) || !regionInputs(request, version, id, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!regionReadHeader(writer, 11, request, budget, 2) || !writer.text(regionText(id)) || !writer.binary(version)) return false;
    written = writer.size();
    return writer.good();
}
inline bool encodeRegionBoundaryChunk(geocaching::ByteView request, geocaching::ByteView id, geocaching::ByteView version,
                                      uint32_t offset, uint16_t length, uint16_t budget, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!regionId(id) || !regionInputs(request, version, id, output, capacity) || !length || length > 1024 || offset >= 8388608 || length > 8388608 - offset) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!regionReadHeader(writer, 12, request, budget, 4) || !writer.text(regionText(id)) || !writer.binary(version) ||
        !writer.unsignedInteger(offset) || !writer.unsignedInteger(length)) return false;
    written = writer.size();
    return writer.good();
}
inline bool regionQueryFilters(const ReadQuery& query)
{
    return query.kind >= -1 && query.kind <= 1 && query.state >= -1 && query.state <= 1 && (!query.author.size || readSpan(query.author, 64)) &&
           query.limit && query.limit <= 32 && query.snapshot <= INT64_MAX &&
           (!query.has_cursor || (query.has_snapshot && readSpan(query.cursor_object, 32) && readSpan(query.cursor_binding, 32)));
}
inline bool regionQueryInputs(const ReadQuery& query, uint8_t* output, size_t capacity)
{
    return regionQueryFilters(query) && networkInputDisjoint({reinterpret_cast<const uint8_t*>(&query), sizeof(query)}, output, capacity) &&
           readOptionalDisjoint(query.author, output, capacity) && readOptionalDisjoint(query.cursor_object, output, capacity) && readOptionalDisjoint(query.cursor_binding, output, capacity);
}
inline bool encodeRegionQuery(geocaching::ByteView request, geocaching::ByteView id, geocaching::ByteView version, const ReadQuery& query,
                              uint16_t budget, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!regionId(id) || !regionInputs(request, version, id, output, capacity) || !regionQueryInputs(query, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!regionReadHeader(writer, 13, request, budget, 9) || !writer.text(regionText(id)) || !writer.binary(version) || !writer.text("intersects") ||
        !(query.kind < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.kind))) ||
        !(query.author.size ? writer.binary(query.author) : writer.nil()) || !(query.state < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.state))) ||
        !writer.unsignedInteger(query.limit) || !(query.has_snapshot ? writer.unsignedInteger(query.snapshot) : writer.nil())) return false;
    if (query.has_cursor)
    {
        if (!writer.array(2) || !writer.binary(query.cursor_object) || !writer.binary(query.cursor_binding)) return false;
    }
    else if (!writer.nil()) return false;
    written = writer.size();
    return writer.good();
}
template <class Hash>
bool deriveRegionQueryBinding(geocaching::ByteView id, geocaching::ByteView version, const ReadQuery& query,
                              Hash& hash, uint8_t* workspace, size_t capacity, uint8_t output[32])
{
    if (!regionId(id) || !regionVersion(version) || !regionQueryInputs(query, workspace, capacity) ||
        !networkInputDisjoint(id, workspace, capacity) || !networkInputDisjoint(version, workspace, capacity) ||
        !networkInputDisjoint(id, output, 32) || !networkInputDisjoint(version, output, 32) ||
        !regionQueryInputs(query, output, 32) || !networkInputDisjoint({output, 32}, workspace, capacity)) return false;
    geocaching::protocol::CmpWriter writer(workspace, capacity);
    if (!writer.array(6) || !writer.text(regionText(id)) || !writer.binary(version) || !writer.text("intersects") ||
        !(query.kind < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.kind))) ||
        !(query.author.size ? writer.binary(query.author) : writer.nil()) || !(query.state < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.state)))) return false;
    const bool ok = hash.start() && hash.update(workspace, writer.size()) && hash.finish(output);
    hash.reset();
    if (!ok) std::memset(output, 0, 32);
    return ok;
}
} // namespace track_sharing
