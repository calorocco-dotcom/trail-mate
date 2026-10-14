#pragma once
#include "track_sharing/manifest.h"
#include <cstring>
namespace track_sharing
{
inline bool signingSpansOverlap(const uint8_t* a, size_t a_size, const uint8_t* b, size_t b_size)
{
    const auto first = reinterpret_cast<uintptr_t>(a), second = reinterpret_cast<uintptr_t>(b);
    return a_size && b_size && (first <= second ? second - first < a_size : first - second < b_size);
}
// Cryptographic primitive, not publication authorization. Caller must first
// verify uploaded file/hash/geometry and reserve the author revision. Identity
// retains private keys. All workspace/view/signature storage is caller-owned.
template <class Signer>
bool signManifest(geocaching::ByteView raw, Signer& identity, ManifestView& view,
                  uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity)
{
    constexpr char domain[] = "TrailMate.Track.Manifest.v1";
    if (!workspace || !signature || signature_capacity < 64 || raw.size > 4096 || capacity < sizeof(domain) + raw.size || capacity < 64 ||
        signingSpansOverlap(raw.data, raw.size, workspace, capacity) || signingSpansOverlap(raw.data, raw.size, signature, 64) ||
        signingSpansOverlap(workspace, capacity, signature, 64) || !identity.isReady() || !decodeManifest(raw, view)) return false;
    identity.combinedPublicKey(workspace);
    if (std::memcmp(workspace, view.author.data, 64)) return false;
    std::memcpy(workspace, domain, sizeof(domain));
    if (view.version == 2) workspace[sizeof(domain) - 2] = '2';
    std::memcpy(workspace + sizeof(domain), raw.data, raw.size);
    return identity.sign(workspace, sizeof(domain) + raw.size, signature);
}
// Caller has completed file/revision authorization under an immutable lease.
// Generation binds even a restored identical key to a distinct author epoch.
template <class Signer>
bool signManifestForGeneration(geocaching::ByteView raw, Signer& identity, uint32_t generation, ManifestView& view,
                               uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity)
{
    if (!signature || signature_capacity < 64 || signingSpansOverlap(raw.data, raw.size, signature, 64) ||
        signingSpansOverlap(workspace, capacity, signature, 64)) return false;
    std::memset(signature, 0, 64);
    if (!generation || identity.authorGeneration() != generation || !identity.isReady()) return false;
    if (!signManifest(raw, identity, view, workspace, capacity, signature, signature_capacity) || identity.authorGeneration() != generation || !identity.isReady())
    {
        std::memset(signature, 0, 64);
        return false;
    }
    return true;
}
} // namespace track_sharing
