#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "hostlink/management_envelope.h"
namespace hostlink::management
{
struct GeocacheSignRequest
{
    geocaching::ByteView transfer, hash, operation, draft;
    uint32_t bytes = 0, author_generation = 0;
    uint64_t draft_generation = 0;
};
// [sealed_transfer16, unsigned_record_hash32, bytes, author_generation, operation16].
inline Status parseGeocacheSignRequest(const uint8_t* input, size_t size, GeocacheSignRequest& output)
{
    if (input)
    {
        const auto in = reinterpret_cast<uintptr_t>(input), out = reinterpret_cast<uintptr_t>(&output);
        if (in <= out ? out - in < size : in - out < sizeof(output)) return Status::InvalidField;
    }
    output = {};
    if (!input || !size || size > 448) return Status::InvalidField;
    geocaching::protocol::CmpReader reader({input, size});
    size_t count = 0;
    uint64_t bytes = 0, generation = 0;
    GeocacheSignRequest parsed;
    if (!reader.array(count, 7) || (count != 5 && count != 7) || !reader.binary(parsed.transfer, 16) || parsed.transfer.size != 16 ||
        !reader.binary(parsed.hash, 32) || parsed.hash.size != 32 || !reader.unsignedInteger(bytes) || !bytes || bytes > 4096 ||
        !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.binary(parsed.operation, 16) || parsed.operation.size != 16) return Status::InvalidField;
    if (count == 7 && (!reader.binary(parsed.draft, 16) || parsed.draft.size != 16 ||
                       !reader.unsignedInteger(parsed.draft_generation) || !parsed.draft_generation)) return Status::InvalidField;
    if (!reader.finished()) return Status::InvalidField;
    uint8_t transfer_bits = 0, operation_bits = 0;
    for (unsigned index = 0; index < 16; ++index)
    {
        transfer_bits |= parsed.transfer.data[index];
        operation_bits |= parsed.operation.data[index];
    }
    if (!transfer_bits || !operation_bits) return Status::InvalidField;
    parsed.bytes = static_cast<uint32_t>(bytes);
    parsed.author_generation = static_cast<uint32_t>(generation);
    output = parsed;
    return Status::Ok;
}
} // namespace hostlink::management
