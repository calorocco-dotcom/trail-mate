#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "hostlink/management_envelope.h"
#include <cstring>

namespace hostlink::management
{
struct RouteInstallView
{
    geocaching::ByteView transfer{}, hash{};
    uint32_t bytes = 0, generation = 0;
};
static_assert(sizeof(RouteInstallView) <= 48, "Route request must remain a small borrowed view");
// Borrowed request bytes: do not overwrite the command arena before consuming
// these bindings. This decoder authorizes nothing and performs no SD writes.
inline Status parseRouteInstallRequest(geocaching::ByteView input, RouteInstallView& output)
{
    output = {};
    if (!input.data || input.size > 448) return Status::InvalidField;
    geocaching::protocol::CmpReader reader(input);
    RouteInstallView parsed;
    size_t fields = 0;
    uint64_t total = 0, generation = 0;
    if (!reader.array(fields, 4) || fields != 4 ||
        !reader.binary(parsed.transfer, 16) || parsed.transfer.size != 16 ||
        !reader.binary(parsed.hash, 32) || parsed.hash.size != 32 ||
        !reader.unsignedInteger(total) || !total || total > 8388608 ||
        !reader.unsignedInteger(generation) || !generation || generation >= UINT32_MAX ||
        !reader.finished()) return Status::InvalidField;
    uint8_t nonzero = 0;
    for (size_t index = 0; index < parsed.transfer.size; ++index) nonzero |= parsed.transfer.data[index];
    if (!nonzero) return Status::InvalidField;
    parsed.bytes = static_cast<uint32_t>(total);
    parsed.generation = static_cast<uint32_t>(generation);
    output = parsed;
    return Status::Ok;
}
inline bool routeInstallTarget(geocaching::ByteView hash, char* output, size_t capacity)
{
    if (!hash.data || hash.size != 32 || !output) return false;
    const auto source = reinterpret_cast<uintptr_t>(hash.data), target = reinterpret_cast<uintptr_t>(output);
    const size_t span = capacity < 77 ? capacity : 77;
    if (span && (source <= target ? target - source < 32 : source - target < span)) return false;
    if (capacity < 77)
    {
        if (capacity) output[0] = '\0';
        return false;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::memcpy(output, "/routes/", 8);
    for (size_t index = 0; index < 32; ++index)
    {
        output[8 + index * 2] = digits[hash.data[index] >> 4];
        output[9 + index * 2] = digits[hash.data[index] & 15];
    }
    std::memcpy(output + 72, ".kml", 5);
    return true;
}
} // namespace hostlink::management
