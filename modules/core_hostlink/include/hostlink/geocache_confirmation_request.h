#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "hostlink/management_envelope.h"
namespace hostlink::management
{
// Confirmation reuses a sealed SignedCache upload (transfer kind 6). The storage worker must
// check its hash, local author reservation and directory signature before commit.
struct GeocacheConfirmationRequest
{
    geocaching::ByteView transfer, hash, proof, signature, directory_public, epoch;
    uint32_t author_generation = 0;
};
// [transfer16, upload_sha256, author_generation, proof_raw, signature64,
// directory_public64, epoch16]. Actual v1 proofs fit 192 bytes.
inline Status parseGeocacheConfirmationRequest(const uint8_t* input, size_t size, GeocacheConfirmationRequest& output)
{
    if (input)
    {
        const auto in = reinterpret_cast<uintptr_t>(input), out = reinterpret_cast<uintptr_t>(&output);
        if (in <= out ? out - in < size : in - out < sizeof(output)) return Status::InvalidField;
    }
    output = {};
    if (!input || !size || size > 448) return Status::InvalidField;
    geocaching::protocol::CmpReader reader({input,size});
    size_t count = 0;
    uint64_t generation = 0;
    GeocacheConfirmationRequest value;
    if (!reader.array(count,7) || count != 7 || !reader.binary(value.transfer,16) || value.transfer.size != 16 ||
        !reader.binary(value.hash,32) || value.hash.size != 32 || !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.binary(value.proof,192) || !value.proof.size || !reader.binary(value.signature,64) || value.signature.size != 64 ||
        !reader.binary(value.directory_public,64) || value.directory_public.size != 64 || !reader.binary(value.epoch,16) || value.epoch.size != 16 || !reader.finished()) return Status::InvalidField;
    uint8_t present = 0;
    for(unsigned i=0;i<16;++i) present |= value.transfer.data[i];
    if(!present) return Status::InvalidField;
    value.author_generation = static_cast<uint32_t>(generation);
    output = value;
    return Status::Ok;
}
} // namespace hostlink::management
