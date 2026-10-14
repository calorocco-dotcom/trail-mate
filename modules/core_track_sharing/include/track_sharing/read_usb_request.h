#pragma once
#include "track_sharing/read_response_validator.h"

namespace track_sharing
{
// Caller admits this small decode workspace in PSRAM. Views are borrowed only
// until TrackReadOwner copies them; no frame view survives command execution.
struct ReadUsbRequest
{
    geocaching::ByteView request, directory, epoch;
    ReadQuery query;
    ReadValidationContext expected;
    RegionReadScope region;
    uint32_t generation = 0;
    uint8_t operation = 0;
};
static_assert(sizeof(ReadUsbRequest) <= 384, "USB decode workspace contains views only");
inline bool readUsbNil(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw)
{
    return reader.position() < raw.size && raw.data[reader.position()] == 0xc0;
}
inline bool readUsbOptionalBinary(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw,
                                  geocaching::ByteView& output, size_t size)
{
    if (readUsbNil(reader, raw))
    {
        output = {};
        return reader.nil();
    }
    return reader.binary(output, size) && output.size == size;
}
inline bool readUsbFilter(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw, int8_t& output)
{
    if (readUsbNil(reader, raw))
    {
        output = -1;
        return reader.nil();
    }
    uint64_t value = 0;
    if (!reader.unsignedInteger(value) || value > 1) return false;
    output = int8_t(value);
    return true;
}
// [networkRequest16,directory16,epoch16,generation,readOperation,parameters]
// Query parameters match directory GET_QUERY; GET_FILE additionally includes
// signed expected file bytes, which are not sent as part of the network body.
inline bool decodeReadUsbRegion(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw, ReadUsbRequest& output)
{
    auto& region = output.region;
    size_t fields = 0;
    const size_t expected = output.operation == 10 ? 7 : output.operation == 11 ? 4
                                                     : output.operation == 12   ? 7
                                                                                : 10;
    if (!reader.array(fields, expected) || fields != expected || !reader.text(region.name_space, 24) || !regionNamespace(region.name_space)) return false;
    if (output.operation == 10)
    {
        int64_t level = 0;
        uint64_t depth = 0, limit = 0;
        if (!reader.binary(region.version, 32) || !regionVersion(region.version) || !decodeRegionOptionalId(reader, raw, region.parent) ||
            !reader.signedInteger(level) || level < -1 || level > 8 || !decodeRegionOptionalId(reader, raw, region.after) ||
            !reader.unsignedInteger(depth) || depth > 8 || level > int64_t(depth) || !reader.unsignedInteger(limit) || !limit || limit > 32 ||
            (region.parent.empty() != (level == -1)) || (!region.parent.empty() && !regionInNamespace(region.parent, region.name_space)) ||
            (!region.after.empty() && !regionInNamespace(region.after, region.name_space))) return false;
        region.parent_level = int8_t(level);
        region.depth = uint8_t(depth);
        output.query.limit = uint8_t(limit);
        output.expected.query = &output.query;
    }
    else
    {
        if (!reader.text(region.id, 89) || !regionInNamespace(region.id, region.name_space) || !reader.binary(region.version, 32) || !regionVersion(region.version)) return false;
        if (output.operation == 11)
        {
            uint64_t depth = 0;
            if (!reader.unsignedInteger(depth) || depth > 8) return false;
            region.depth = uint8_t(depth);
        }
        else if (output.operation == 12)
        {
            uint64_t total = 0, offset = 0, length = 0;
            if (!reader.binary(output.expected.file_hash, 32) || output.expected.file_hash.size != 32 || !reader.unsignedInteger(total) || !total || total > 8388608 ||
                !reader.unsignedInteger(offset) || offset >= total || !reader.unsignedInteger(length) || !length || length > 1024 || length > total - offset) return false;
            output.expected.file_bytes = uint32_t(total);
            output.expected.offset = uint32_t(offset);
            output.expected.length = uint16_t(length);
        }
    }
    output.expected.region = &region;
    return output.operation == 13 || reader.finished();
}
inline bool decodeReadUsbRequest(geocaching::ByteView raw, ReadUsbRequest& output)
{
    if (!raw.data || !raw.size || raw.size > 471 || !networkInputDisjoint(raw, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    output = {};
    geocaching::protocol::CmpReader reader(raw);
    size_t fields = 0;
    uint64_t generation = 0, operation = 0;
    if (!reader.array(fields, 6) || fields != 6 || !reader.binary(output.request, 16) || !networkRequestId(output.request) ||
        !reader.binary(output.directory, 16) || !networkRequestId(output.directory) || !reader.binary(output.epoch, 16) || !networkRequestId(output.epoch) ||
        !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX || !reader.unsignedInteger(operation) ||
        operation > 13 || !readResponseOperation(uint8_t(operation))) return false;
    output.generation = uint32_t(generation);
    output.operation = uint8_t(operation);
    if (operation == 0 || operation == 9) return reader.array(fields, 0) && !fields && reader.finished();
    if (operation >= 10)
    {
        if (!decodeReadUsbRegion(reader, raw, output)) return false;
        if (operation != 13) return true;
    }
    if (operation == 1 || operation == 13)
    {
        auto& query = output.query;
        int64_t coordinate = 0;
        uint64_t limit = 0;
        if ((operation == 1 && (!reader.array(fields, 7) || fields != 7)) || !reader.array(fields, 4) || fields != 4) return false;
        for (auto* target : {&query.west, &query.south, &query.east, &query.north})
        {
            if (!reader.signedInteger(coordinate) || coordinate < INT32_MIN || coordinate > INT32_MAX) return false;
            *target = int32_t(coordinate);
        }
        if (query.west < -1800000000 || query.west > 1799999999 || query.east < -1800000000 || query.east > 1799999999 ||
            query.south < -900000000 || query.north > 900000000 || query.south > query.north ||
            !readUsbFilter(reader, raw, query.kind) || !readUsbOptionalBinary(reader, raw, query.author, 64) || !readUsbFilter(reader, raw, query.state) ||
            !reader.unsignedInteger(limit) || !limit || limit > 32) return false;
        query.limit = uint8_t(limit);
        if (readUsbNil(reader, raw))
        {
            if (!reader.nil()) return false;
        }
        else
        {
            query.has_snapshot = true;
            if (!reader.unsignedInteger(query.snapshot) || query.snapshot > INT64_MAX) return false;
        }
        if (readUsbNil(reader, raw))
        {
            if (!reader.nil()) return false;
        }
        else
        {
            query.has_cursor = true;
            if (!query.has_snapshot || !reader.array(fields, 2) || fields != 2 || !reader.binary(query.cursor_object, 32) || query.cursor_object.size != 32 ||
                !reader.binary(query.cursor_binding, 32) || query.cursor_binding.size != 32) return false;
        }
        output.expected.query = &query;
    }
    else if (operation == 2)
    {
        if (!reader.array(fields, 2) || fields != 2 || !reader.binary(output.expected.object, 32) || output.expected.object.size != 32 ||
            !readUsbOptionalBinary(reader, raw, output.expected.revision, 32)) return false;
    }
    else
    {
        uint64_t bytes = 0, offset = 0, length = 0;
        if (!reader.array(fields, 4) || fields != 4 || !reader.binary(output.expected.file_hash, 32) || output.expected.file_hash.size != 32 ||
            !reader.unsignedInteger(bytes) || !bytes || bytes > 8388608 || !reader.unsignedInteger(offset) || offset >= bytes ||
            !reader.unsignedInteger(length) || !length || length > 1024 || length > bytes - offset) return false;
        output.expected.file_bytes = uint32_t(bytes);
        output.expected.offset = uint32_t(offset);
        output.expected.length = uint16_t(length);
    }
    return reader.finished();
}
} // namespace track_sharing
