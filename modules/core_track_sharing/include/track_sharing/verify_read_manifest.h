#pragma once
#include "track_sharing/network_read_response.h"
#include "track_sharing/verify_manifest.h"

namespace track_sharing
{
enum class ReadManifestVerification : uint8_t
{
    Valid,
    InvalidResponse,
    InvalidSignature,
    ReferenceMismatch,
    CryptoUnavailable,
    WorkspaceTooSmall
};

// Borrows the canonical signed element from the original response. The owner
// provides verification output/domain workspace/hash context in admitted memory.
template <class Crypto, class Hash>
ReadManifestVerification verifyReadManifest(geocaching::ByteView response, geocaching::ByteView request,
                                            geocaching::ByteView object, geocaching::ByteView revision,
                                            Crypto& crypto, Hash& hash, uint8_t* workspace, size_t capacity,
                                            VerifiedManifestView& output)
{
    output.verified = false;
    if (!object.data || object.size != 32 || (revision.size && (!revision.data || revision.size != 32)) ||
        signingSpansOverlap(object.data, object.size, reinterpret_cast<const uint8_t*>(&output), sizeof(output)) ||
        signingSpansOverlap(revision.data, revision.size, reinterpret_cast<const uint8_t*>(&output), sizeof(output)) ||
        signingSpansOverlap(object.data, object.size, workspace, capacity) || signingSpansOverlap(revision.data, revision.size, workspace, capacity)) return ReadManifestVerification::InvalidResponse;
    ReadManifestResponse manifest;
    ReadResponseView envelope;
    if (!decodeTrackReadManifestResponse(response, request, manifest) || !decodeTrackReadEnvelope(response, request, 2, envelope) ||
        envelope.body.size < 3 || envelope.body.data[0] != 0x92 || envelope.body.data[envelope.body.size - 1] > 1) return ReadManifestVerification::InvalidResponse;
    // Canonical array(2) and boolean-as-uint(0/1) each occupy one byte.
    const geocaching::ByteView signed_manifest{envelope.body.data + 1, envelope.body.size - 2};
    const auto result = verifyManifest(signed_manifest, crypto, hash, workspace, capacity, output);
    if (result != ManifestVerification::Valid)
    {
        if (result == ManifestVerification::InvalidSignature) return ReadManifestVerification::InvalidSignature;
        if (result == ManifestVerification::WorkspaceTooSmall) return ReadManifestVerification::WorkspaceTooSmall;
        if (result == ManifestVerification::CryptoUnavailable) return ReadManifestVerification::CryptoUnavailable;
        return ReadManifestVerification::InvalidResponse;
    }
    if (std::memcmp(object.data, output.object_id, 32) || (revision.size && std::memcmp(revision.data, output.revision_hash, 32)))
    {
        output.verified = false;
        return ReadManifestVerification::ReferenceMismatch;
    }
    return ReadManifestVerification::Valid;
}
} // namespace track_sharing
