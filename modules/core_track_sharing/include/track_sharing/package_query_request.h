#pragma once
#include "track_sharing/prepare_request.h"
namespace track_sharing
{
inline bool decodePackageQueryRequest(geocaching::ByteView raw, PrepareRequest& output)
{
    const auto a = reinterpret_cast<uintptr_t>(raw.data), b = reinterpret_cast<uintptr_t>(&output);
    if (raw.data && (a <= b ? b - a < raw.size : a - b < sizeof(output))) return false;
    output = {};
    if (!raw.data || raw.size > 110) return false;
    geocaching::protocol::CmpReader reader(raw);
    size_t fields = 0;
    uint64_t generation = 0;
    if (!reader.array(fields, 4) || fields != 4 || !reader.binary(output.operation, 16) || output.operation.size != 16 || !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.binary(output.expected_object, 32) || output.expected_object.size != 32 || !reader.binary(output.expected_revision, 32) || output.expected_revision.size != 32 || !reader.finished()) return false;
    for (const auto id : {output.operation, output.expected_object, output.expected_revision})
    {
        uint8_t bits = 0;
        for (size_t i = 0; i < id.size; ++i) bits |= id.data[i];
        if (!bits) return false;
    }
    output.generation = generation;
    output.package_request = true;
    output.install_request = true;
    output.query_request = true;
    return true;
}
} // namespace track_sharing
