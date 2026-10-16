#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include <cstring>
#include <initializer_list>
namespace track_sharing
{
struct IpConfirmationRequest
{
    geocaching::ByteView operation, transfer, proof_hash, directory_public,
        directory, epoch, network_request, object, revision_hash;
    uint32_t generation = 0, revision = 0;
    geocaching::ByteView manifest_transfer{}, manifest_hash{}, manifest_signature{};
};
static_assert(sizeof(IpConfirmationRequest) <= 208, "Confirmation request borrows bytes only");
// [local_sign_operation16, sealed_receipt_upload16, upload_sha256,
//  author_generation, directory_public64, directory_delivery16, directory_epoch16,
//  network_publish_request16, object32, revision_hash32, revision].
// Shape validation only: worker must authenticate upload, local signed manifest,
// reservation, current identity, directory delivery and signed receipt.
inline bool decodeIpConfirmationRequest(geocaching::ByteView body, IpConfirmationRequest& output, bool import_history = false)
{
    if (body.data)
    {
        const auto source = reinterpret_cast<uintptr_t>(body.data), target = reinterpret_cast<uintptr_t>(&output);
        if (source <= target ? target - source < body.size : source - target < sizeof(output)) return false;
    }
    output = {};
    if (!body.data || !body.size || body.size > (import_history ? 416 : 288)) return false;
    geocaching::protocol::CmpReader reader(body);
    IpConfirmationRequest candidate;
    size_t fields = 0;
    uint64_t generation = 0, revision = 0;
    if (!reader.array(fields, 14) || fields != (import_history ? 14 : 11) ||
        !reader.binary(candidate.operation, 16) || candidate.operation.size != 16 ||
        !reader.binary(candidate.transfer, 16) || candidate.transfer.size != 16 ||
        !reader.binary(candidate.proof_hash, 32) || candidate.proof_hash.size != 32 ||
        !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.binary(candidate.directory_public, 64) || candidate.directory_public.size != 64 ||
        !reader.binary(candidate.directory, 16) || candidate.directory.size != 16 ||
        !reader.binary(candidate.epoch, 16) || candidate.epoch.size != 16 ||
        !reader.binary(candidate.network_request, 16) || candidate.network_request.size != 16 ||
        !reader.binary(candidate.object, 32) || candidate.object.size != 32 ||
        !reader.binary(candidate.revision_hash, 32) || candidate.revision_hash.size != 32 ||
        !reader.unsignedInteger(revision) || !revision || revision > UINT32_MAX) return false;
    if (import_history && (!reader.binary(candidate.manifest_transfer, 16) || candidate.manifest_transfer.size != 16 ||
                           !reader.binary(candidate.manifest_hash, 32) || candidate.manifest_hash.size != 32 ||
                           !reader.binary(candidate.manifest_signature, 64) || candidate.manifest_signature.size != 64)) return false;
    if (!reader.finished()) return false;
    if (import_history)
        for (const auto field : {candidate.manifest_transfer, candidate.manifest_hash, candidate.manifest_signature})
        {
            uint8_t present = 0;
            for (size_t i = 0; i < field.size; ++i) present |= field.data[i];
            if (!present) return false;
        }
    for (const auto field : {candidate.operation, candidate.transfer, candidate.proof_hash, candidate.directory_public,
                             candidate.directory, candidate.epoch, candidate.network_request, candidate.object, candidate.revision_hash})
    {
        uint8_t present = 0;
        for (size_t i = 0; i < field.size; ++i) present |= field.data[i];
        if (!present) return false;
    }
    candidate.generation = uint32_t(generation);
    candidate.revision = uint32_t(revision);
    output = candidate;
    return true;
}
} // namespace track_sharing
