#pragma once
#include "track_sharing/manifest_hash.h"
namespace track_sharing
{
enum class ManifestVerification : uint8_t
{
    Valid,
    InvalidManifest,
    InvalidSignature,
    IdentityMismatch,
    WorkspaceTooSmall,
    CryptoUnavailable
};
struct VerifiedManifestView
{
    ManifestView manifest;
    geocaching::ByteView signature;
    uint8_t object_id[32]{}, revision_hash[32]{};
    bool verified = false;
};
static_assert(sizeof(VerifiedManifestView) <= 304, "Verified manifest must borrow all content");
// Caller admits output and domain workspace separately; none is an automatic
// protocol object here. Crypto verifies Ed25519, Hash lends its stream context.
// Successful verification establishes author integrity, not file/currentness.
template <class Crypto, class Hash>
ManifestVerification verifyManifest(geocaching::ByteView signed_manifest, Crypto& crypto, Hash& hash,
                                    uint8_t* workspace, size_t capacity, VerifiedManifestView& output,
                                    geocaching::ByteView expected_author = {})
{
    if (signingSpansOverlap(signed_manifest.data, signed_manifest.size, reinterpret_cast<const uint8_t*>(&output), sizeof(output)))
    {
        output.verified = false;
        return ManifestVerification::InvalidManifest;
    }
    output = {};
    if (!signed_manifest.data || !signed_manifest.size || signed_manifest.size > 4166) return ManifestVerification::InvalidManifest;
    geocaching::protocol::CmpReader reader(signed_manifest);
    size_t fields = 0;
    geocaching::ByteView raw, signature;
    if (!reader.array(fields, 2) || fields != 2 || !reader.binary(raw, 4096) || !reader.binary(signature, 64) || signature.size != 64 || !reader.finished() || !decodeManifest(raw, output.manifest)) return ManifestVerification::InvalidManifest;
    if (expected_author.size && (expected_author.size != 64 || !expected_author.data || std::memcmp(expected_author.data, output.manifest.author.data, 64))) return ManifestVerification::IdentityMismatch;
    constexpr char domain[] = "TrailMate.Track.Manifest.v1";
    if (!workspace || capacity < sizeof(domain) + raw.size || signingSpansOverlap(signed_manifest.data, signed_manifest.size, workspace, capacity) ||
        signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(&output), sizeof(output))) return ManifestVerification::WorkspaceTooSmall;
    std::memcpy(workspace, domain, sizeof(domain));
    if (output.manifest.version == 2) workspace[sizeof(domain) - 2] = '2';
    std::memcpy(workspace + sizeof(domain), raw.data, raw.size);
    const auto verified = crypto.verify({output.manifest.author.data + 32, 32}, signature, {workspace, sizeof(domain) + raw.size});
    if (verified != ManifestVerification::Valid) return verified;
    if (!deriveManifestHashes(raw, output.manifest, hash, output.object_id, output.revision_hash)) return ManifestVerification::CryptoUnavailable;
    output.signature = signature;
    output.verified = true;
    return ManifestVerification::Valid;
}
} // namespace track_sharing
