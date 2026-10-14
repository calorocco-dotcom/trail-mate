#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include <cstring>
#include <initializer_list>
namespace track_sharing
{
// Borrowed request views. Platform copies bindings into its admitted job before
// command scratch reuse; this decoder grants no storage or signing authority.
struct PrepareRequest
{
    geocaching::ByteView operation, kml_upload, manifest_upload, expected_parent;
    uint32_t generation = 0;
    geocaching::ByteView package_upload{};
    bool package_request = false;
    bool install_request = false;
    bool query_request = false;
    geocaching::ByteView expected_object{}, expected_revision{};
};
static_assert(sizeof(PrepareRequest) <= 128, "Prepare request cannot own manifest bytes");
inline bool decodePrepareRequest(geocaching::ByteView body, PrepareRequest& output)
{
    const auto source = reinterpret_cast<uintptr_t>(body.data), target = reinterpret_cast<uintptr_t>(&output);
    if (body.data && (source <= target ? target - source < body.size : source - target < sizeof(output))) return false;
    output = {};
    if (!body.data || !body.size || body.size > 219) return false;
    geocaching::protocol::CmpReader reader(body);
    size_t fields = 0;
    uint64_t generation = 0;
    if (!reader.array(fields, 8) || (fields != 5 && fields != 6 && fields != 8) ||
        !reader.binary(output.operation, 16) || output.operation.size != 16 ||
        !reader.binary(output.kml_upload, 16) || output.kml_upload.size != 16 ||
        !reader.binary(output.manifest_upload, 16) || output.manifest_upload.size != 16 ||
        !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX) return false;
    for (const auto id : {output.operation, output.kml_upload, output.manifest_upload})
    {
        uint8_t bits = 0;
        for (size_t i = 0; i < 16; ++i) bits |= id.data[i];
        if (!bits) return false;
    }
    if (!std::memcmp(output.kml_upload.data, output.manifest_upload.data, 16)) return false;
    if (reader.position() < body.size && body.data[reader.position()] == 0xc0)
    {
        if (!reader.nil()) return false;
    }
    else
    {
        if (!reader.binary(output.expected_parent, 32) || output.expected_parent.size != 32) return false;
        uint8_t bits = 0;
        for (size_t i = 0; i < 32; ++i) bits |= output.expected_parent.data[i];
        if (!bits) return false;
    }
    if (fields >= 6)
    {
        output.package_request = true;
        if (reader.position() < body.size && body.data[reader.position()] == 0xc0)
        {
            if (!reader.nil()) return false;
        }
        else
        {
            if (!reader.binary(output.package_upload, 16) || output.package_upload.size != 16) return false;
            uint8_t bits = 0;
            for (size_t i = 0; i < 16; ++i) bits |= output.package_upload.data[i];
            if (!bits) return false;
        }
    }
    if (fields == 8)
    {
        output.install_request = true;
        if (output.expected_parent.size || !reader.binary(output.expected_object, 32) || output.expected_object.size != 32 ||
            !reader.binary(output.expected_revision, 32) || output.expected_revision.size != 32) return false;
        for (const auto id : {output.expected_object, output.expected_revision})
        {
            uint8_t bits = 0;
            for (size_t i = 0; i < 32; ++i) bits |= id.data[i];
            if (!bits) return false;
        }
    }
    if (!reader.finished()) return false;
    output.generation = uint32_t(generation);
    return true;
}
} // namespace track_sharing
