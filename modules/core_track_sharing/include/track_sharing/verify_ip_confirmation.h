#pragma once
#include "track_sharing/ip_confirmation_request.h"
#include "track_sharing/verify_manifest.h"
#include "track_sharing/verify_receipt.h"
namespace track_sharing
{
// Caller supplies authenticated CURRENT identity and an immutable verified local
// signed manifest. No RF service, owned payload buffer or heap allocation.
// Successful outputs lend the unchanged signed receipt until persistence ends.
template <class Crypto, class Hash>
ReceiptVerification verifyIpConfirmation(const IpConfirmationRequest& request, geocaching::ByteView signed_receipt,
                                         const VerifiedManifestView& manifest, uint32_t generation,
                                         geocaching::ByteView author, geocaching::ByteView author_hash,
                                         Crypto& crypto, Hash& hash, uint8_t* workspace, size_t capacity,
                                         PublicationJournal& expected, VerifiedReceiptView& proof)
{
    const auto job_bytes = reinterpret_cast<const uint8_t*>(&expected);
    const auto proof_bytes = reinterpret_cast<const uint8_t*>(&proof);
    if (signingSpansOverlap(job_bytes, sizeof(expected), proof_bytes, sizeof(proof))) return ReceiptVerification::InvalidReceipt;
    for (const auto input : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&request), sizeof(request)},
                             {reinterpret_cast<const uint8_t*>(&manifest), sizeof(manifest)},
                             request.operation,
                             request.transfer,
                             request.proof_hash,
                             request.directory_public,
                             request.directory,
                             request.epoch,
                             request.network_request,
                             request.object,
                             request.revision_hash,
                             signed_receipt,
                             manifest.manifest.raw,
                             manifest.signature,
                             author,
                             author_hash})
        if (signingSpansOverlap(input.data, input.size, job_bytes, sizeof(expected)) ||
            signingSpansOverlap(input.data, input.size, proof_bytes, sizeof(proof))) return ReceiptVerification::InvalidReceipt;
    expected = {};
    proof = {};
    if (!manifest.verified || !generation || request.generation != generation ||
        !author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 ||
        !manifest.manifest.author.data || manifest.manifest.author.size != 64 ||
        std::memcmp(author.data, manifest.manifest.author.data, 64) ||
        !signed_receipt.data || !signed_receipt.size || signed_receipt.size > 1100 ||
        !manifest.manifest.file_hash.data || manifest.manifest.file_hash.size != 32 ||
        request.revision != manifest.manifest.revision) return ReceiptVerification::BindingMismatch;
    const auto span = [](geocaching::ByteView field, size_t size)
    { return field.data && field.size == size; };
    if (!span(request.operation, 16) || !span(request.transfer, 16) || !span(request.proof_hash, 32) ||
        !span(request.directory_public, 64) || !span(request.directory, 16) || !span(request.epoch, 16) ||
        !span(request.network_request, 16) || !span(request.object, 32) || !span(request.revision_hash, 32)) return ReceiptVerification::BindingMismatch;
    if (std::memcmp(request.object.data, manifest.object_id, 32) ||
        std::memcmp(request.revision_hash.data, manifest.revision_hash, 32)) return ReceiptVerification::BindingMismatch;
    if (!workspace || capacity < sizeof("TrailMate.Track.Receipt.v1") + 1024) return ReceiptVerification::WorkspaceTooSmall;
    for (const auto input : {signed_receipt, author, author_hash, request.operation, request.transfer, request.proof_hash, request.directory_public, request.directory, request.epoch, request.network_request, request.object, request.revision_hash, manifest.manifest.raw, manifest.signature, geocaching::ByteView{reinterpret_cast<const uint8_t*>(&request), sizeof(request)}, {reinterpret_cast<const uint8_t*>(&manifest), sizeof(manifest)}, {job_bytes, sizeof(expected)}, {proof_bytes, sizeof(proof)}})
        if (signingSpansOverlap(input.data, input.size, workspace, capacity)) return ReceiptVerification::WorkspaceTooSmall;
    const auto digest = [&](geocaching::ByteView input, uint8_t* output)
    {
        const bool ok = hash.start() && hash.update(input.data, input.size) && hash.finish(output);
        hash.reset();
        return ok;
    };
    if (!digest(author, workspace) || std::memcmp(workspace, author_hash.data, 32)) return ReceiptVerification::BindingMismatch;
    if (!digest(signed_receipt, workspace) || std::memcmp(workspace, request.proof_hash.data, 32)) return ReceiptVerification::BindingMismatch;
    // Reticulum LXMF delivery derives from the combined public key, not from
    // an arbitrary USB-supplied destination sharing a valid signing key.
    if (!digest(request.directory_public, workspace)) return ReceiptVerification::InvalidReceipt;
    std::memmove(workspace + 10, workspace, 16);
    constexpr char service[] = "lxmf.delivery";
    if (!digest({reinterpret_cast<const uint8_t*>(service), sizeof(service) - 1}, workspace + 32)) return ReceiptVerification::InvalidReceipt;
    std::memcpy(workspace, workspace + 32, 10);
    if (!digest({workspace, 26}, workspace + 32) || std::memcmp(workspace + 32, request.directory.data, 16)) return ReceiptVerification::BindingMismatch;
    std::memcpy(expected.operation, request.operation.data, 16);
    std::memcpy(expected.object, manifest.object_id, 32);
    std::memcpy(expected.revision_hash, manifest.revision_hash, 32);
    std::memcpy(expected.directory, request.directory.data, 16);
    std::memcpy(expected.directory_epoch, request.epoch.data, 16);
    std::memcpy(expected.file_hash, manifest.manifest.file_hash.data, 32);
    std::memcpy(expected.author_hash, author_hash.data, 32);
    expected.sequence = 1;
    expected.generation = generation;
    expected.revision = request.revision;
    expected.file_bytes = expected.offset = manifest.manifest.file_bytes;
    expected.phase = PublicationPhase::Publishing;
    const auto result = verifyReceipt(signed_receipt, expected, request.network_request, request.directory_public,
                                      manifest.manifest.state, crypto, workspace, capacity, proof);
    if (result != ReceiptVerification::Valid)
    {
        expected = {};
        proof = {};
    }
    return result;
}
} // namespace track_sharing
