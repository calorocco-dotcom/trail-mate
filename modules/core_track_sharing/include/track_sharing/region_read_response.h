#pragma once
#include "geocaching/protocol/record_decoder.h"
#include "track_sharing/network_upload_response.h"
#include "track_sharing/region_read_request.h"
#include "track_sharing/network_read_response.h"
#include "track_sharing/read_query_response.h"

namespace track_sharing
{
struct RegionCapabilitiesView
{
    std::string_view name_space;
    geocaching::ByteView version;
    uint16_t chunk_bytes = 0;
    uint8_t depth = 0, page_limit = 0;
};
struct RegionNodeView
{
    std::string_view id, parent;
    int32_t west = 0, south = 0, east = 0, north = 0;
    uint16_t children = 0;
    uint8_t level = 0;
};
struct RegionChildrenView
{
    std::string_view cursor;
    uint8_t rows = 0;
};
struct RegionDescriptorView
{
    RegionNodeView node;
    geocaching::ByteView digest;
    uint32_t bytes = 0;
};
struct RegionReadScope
{
    std::string_view name_space, id, parent, after;
    geocaching::ByteView version;
    int8_t parent_level = -1;
    uint8_t depth = 0;
};
struct RegionReplyWorkspace
{
    RegionCapabilitiesView capabilities;
    RegionNodeView row;
    RegionChildrenView page;
    RegionDescriptorView descriptor;
};
static_assert(sizeof(RegionReadScope) <= 96 && sizeof(RegionReplyWorkspace) <= 256,
              "Owner must admit regional metadata in transient PSRAM");
static_assert(sizeof(RegionCapabilitiesView) <= 48 && sizeof(RegionNodeView) <= 80 && sizeof(RegionChildrenView) <= 32 && sizeof(RegionDescriptorView) <= 112,
              "Region decoders retain borrowed metadata, never a page or geometry");
inline geocaching::ByteView regionBytes(std::string_view value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
inline bool regionNamespace(std::string_view value)
{
    if (value.empty() || value.size() > 24) return false;
    for (size_t i = 0; i < value.size(); ++i)
    {
        const auto c = value[i];
        if (i ? !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-') : !(c >= 'a' && c <= 'z')) return false;
    }
    return true;
}
inline bool regionInNamespace(std::string_view id, std::string_view name_space)
{
    return regionId(regionBytes(id)) && id.size() > name_space.size() && id.substr(0, name_space.size()) == name_space && id[name_space.size()] == ':';
}
inline bool regionLocale(std::string_view value)
{
    size_t start = 0, segments = 0;
    for (size_t i = 0; i <= value.size(); ++i)
    {
        if (i == value.size() || value[i] == '-')
        {
            const size_t length = i - start;
            if (!segments ? length < 2 || length > 3 : length < 2 || length > 8) return false;
            if (++segments > 3) return false;
            start = i + 1;
        }
        else
        {
            const char c = value[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (segments && c >= '0' && c <= '9'))) return false;
        }
    }
    return true;
}
inline bool regionReplyInput(geocaching::ByteView raw, geocaching::ByteView request, void* output, size_t size)
{
    return raw.data && raw.size && raw.size <= 8192 && networkRequestId(request) &&
           networkInputDisjoint(raw, static_cast<uint8_t*>(output), size) && networkInputDisjoint(request, static_cast<uint8_t*>(output), size);
}
inline bool decodeRegionCapabilities(geocaching::ByteView raw, geocaching::ByteView request, RegionCapabilitiesView& output)
{
    if (!regionReplyInput(raw, request, &output, sizeof(output))) return false;
    output = {};
    geocaching::protocol::CmpReader reader(raw);
    uint64_t value = 0;
    std::string_view type;
    size_t count = 0;
    if (!uploadResponseHeader(reader, 9, request, 8) || !reader.unsignedInteger(value) || value != 1 ||
        !reader.text(type, 32) || type != "trailmate.track.regions.v1" || !reader.text(output.name_space, 24) || !regionNamespace(output.name_space) ||
        !reader.binary(output.version, 32) || !regionVersion(output.version) || !reader.unsignedInteger(value) || value > 8) return false;
    output.depth = uint8_t(value);
    if (!reader.unsignedInteger(value) || !value || value > 32) return false;
    output.page_limit = uint8_t(value);
    if (!reader.unsignedInteger(value) || !value || value > 1024) return false;
    output.chunk_bytes = uint16_t(value);
    if (!reader.array(count, 5) || count != 5) return false;
    uint16_t operations = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (!reader.unsignedInteger(value) || value < 9 || value > 13 || (operations & (1U << value))) return false;
        operations |= uint16_t(1U << value);
    }
    return operations == 0x3e00 && reader.finished();
}
inline bool decodeRegionOptionalId(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw, std::string_view& output)
{
    if (reader.position() < raw.size && raw.data[reader.position()] == 0xc0)
    {
        output = {};
        return reader.nil();
    }
    return reader.text(output, 89) && regionId(regionBytes(output));
}
inline bool decodeRegionNode(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw, std::string_view name_space,
                             uint8_t depth, RegionNodeView& output, uint16_t* elements = nullptr)
{
    if (!regionNamespace(name_space) || depth > 8 || !networkInputDisjoint(raw, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(regionBytes(name_space),reinterpret_cast<uint8_t*>(&output),sizeof(output))) return false;
    output = {};
    size_t fields = 0, names = 0;
    uint64_t value = 0;
    std::string_view name;
    if (!reader.array(fields, 7) || fields != 7 || !reader.text(output.id, 89) || !regionInNamespace(output.id, name_space) ||
        !decodeRegionOptionalId(reader, raw, output.parent) || (!output.parent.empty() && !regionInNamespace(output.parent, name_space)) || output.id == output.parent ||
        !reader.unsignedInteger(value) || value > depth || (output.parent.empty() != (value == 0))) return false;
    output.level = uint8_t(value);
    if (!reader.text(name, 128) || !geocaching::protocol::validRecordText(name, false, true) || !reader.array(names, 32)) return false;
    if (elements)
    {
        const size_t count = 11 + 3 * names;
        if (*elements > 2048 || count > size_t(2048 - *elements)) return false;
        *elements += uint16_t(count);
    }
    const size_t names_start = reader.position();
    for (size_t i = 0; i < names; ++i)
    {
        const size_t prefix_end = reader.position();
        std::string_view locale;
        if (!reader.array(fields, 2) || fields != 2 || !reader.text(locale, 21) || !regionLocale(locale) ||
            !reader.text(name, 128) || !geocaching::protocol::validRecordText(name, false, true)) return false;
        // Re-scan the bounded prior translation prefix rather than retain an
        // array of locale spans on the ESP stack or in permanent storage.
        geocaching::protocol::CmpReader previous({raw.data + names_start, prefix_end - names_start});
        for (size_t j = 0; j < i; ++j)
        {
            std::string_view old_locale, old_name;
            if (!previous.array(fields, 2) || fields != 2 || !previous.text(old_locale, 21) || !previous.text(old_name, 128) || old_locale == locale) return false;
        }
        if (!previous.finished()) return false;
    }
    if (!reader.array(fields, 4) || fields != 4) return false;
    int64_t coordinate = 0;
    for (auto* target : {&output.west, &output.south, &output.east, &output.north})
    {
        if (!reader.signedInteger(coordinate) || coordinate < INT32_MIN || coordinate > INT32_MAX) return false;
        *target = int32_t(coordinate);
    }
    if (output.west < -1800000000 || output.west > 1799999999 || output.east < -1800000000 || output.east > 1799999999 ||
        output.south < -900000000 || output.north > 900000000 || output.south > output.north ||
        !reader.unsignedInteger(value) || value > 10000 || (output.level == depth && value)) return false;
    output.children = uint16_t(value);
    return true;
}
inline bool decodeRegionChildren(geocaching::ByteView raw, geocaching::ByteView request, geocaching::ByteView version,
                                 std::string_view name_space, std::string_view parent, std::string_view after,
                                 int8_t parent_level, uint8_t depth, uint8_t limit, RegionNodeView& row, RegionChildrenView& output)
{
    if (!regionReplyInput(raw, request, &output, sizeof(output)) || !regionReplyInput(raw, request, &row, sizeof(row)) || !regionVersion(version) ||
        !regionNamespace(name_space) || depth > 8 || !limit || limit > 32 ||
        (parent.empty() ? parent_level != -1 : parent_level < 0 || parent_level > depth || !regionInNamespace(parent, name_space)) ||
        (!after.empty() && !regionInNamespace(after, name_space)) ||
        !networkInputDisjoint(version, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(version, reinterpret_cast<uint8_t*>(&row), sizeof(row)) ||
        !networkInputDisjoint({reinterpret_cast<const uint8_t*>(&row),sizeof(row)}, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    for (const auto value : {name_space,parent,after})
        if (!readOptionalDisjoint(regionBytes(value),reinterpret_cast<uint8_t*>(&row),sizeof(row)) ||
            !readOptionalDisjoint(regionBytes(value),reinterpret_cast<uint8_t*>(&output),sizeof(output))) return false;
    output = {};
    geocaching::protocol::CmpReader reader(raw);
    geocaching::ByteView received;
    std::string_view previous = after;
    size_t rows = 0;
    if (!uploadResponseHeader(reader, 10, request, 3) || !reader.binary(received, 32) || received.size != 32 || std::memcmp(received.data, version.data, 32) ||
        !decodeRegionOptionalId(reader, raw, output.cursor) || !reader.array(rows, limit)) return false;
    uint16_t elements = uint16_t(9 + rows);
    for (size_t i = 0; i < rows; ++i)
    {
        if (!decodeRegionNode(reader, raw, name_space, depth, row, &elements) || row.parent != parent || row.level != parent_level + 1 ||
            (!previous.empty() && row.id <= previous)) return false;
        previous = row.id;
    }
    if ((!output.cursor.empty() && (!rows || output.cursor != previous || !regionInNamespace(output.cursor, name_space))) || !reader.finished()) return false;
    output.rows = uint8_t(rows);
    return true;
}
inline bool decodeRegionDescriptor(geocaching::ByteView raw, geocaching::ByteView request, std::string_view id, geocaching::ByteView version,
                                   std::string_view name_space, uint8_t depth, RegionDescriptorView& output)
{
    if (!regionReplyInput(raw, request, &output, sizeof(output)) || !regionVersion(version) || !regionInNamespace(id, name_space) ||
        !networkInputDisjoint(version, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(regionBytes(id), reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(regionBytes(name_space), reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    output = {};
    geocaching::protocol::CmpReader reader(raw);
    geocaching::ByteView received;
    std::string_view type;
    size_t fields = 0;
    uint64_t bytes = 0;
    if (!uploadResponseHeader(reader, 11, request, 3) || !reader.binary(received, 32) || received.size != 32 || std::memcmp(received.data, version.data, 32) ||
        !decodeRegionNode(reader, raw, name_space, depth, output.node) || output.node.id != id ||
        !reader.array(fields, 3) || fields != 3 || !reader.text(type, 24) || type != "region-e7-json-v1" ||
        !reader.binary(output.digest, 32) || output.digest.size != 32 || !reader.unsignedInteger(bytes) || !bytes || bytes > 8388608 || !reader.finished()) return false;
    output.bytes = uint32_t(bytes);
    return true;
}
inline bool decodeRegionBoundaryResponse(geocaching::ByteView raw, geocaching::ByteView request, geocaching::ByteView digest,
                                         uint32_t bytes, uint32_t offset, uint16_t length, ReadFileResponse& output)
{
    return decodeTrackReadFileResponse(raw,request,digest,bytes,offset,length,output,12);
}
// The caller owns the immutable region ID/version scope and derives binding
// with deriveRegionQueryBinding. The box is a prefilter only; exact geometry is
// a directory claim, not an author signature or a device-side GIS computation.
inline bool decodeRegionQueryResponse(geocaching::ByteView raw, geocaching::ByteView request, const ReadQuery& query,
                                      geocaching::ByteView binding, ReadQueryRow& row, ReadQueryResponse& output)
{
    return decodeTrackReadQueryResponse(raw,request,query,binding,row,output,13);
}
} // namespace track_sharing
