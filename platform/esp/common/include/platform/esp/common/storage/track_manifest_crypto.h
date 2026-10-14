#pragma once
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "track_sharing/verify_manifest.h"
namespace platform::esp::storage
{
// Public-key verification only. Uses the same Ed25519 implementation as device
// identity; no identity object, private key copy or persistent scratch exists.
struct TrackManifestCrypto
{
    track_sharing::ManifestVerification verify(geocaching::ByteView key, geocaching::ByteView signature, geocaching::ByteView message) const
    {
        constexpr char domain[] = "TrailMate.Track.Manifest.v1";
        if (!key.data || key.size != 32 || !signature.data || signature.size != 64 || !message.data ||
            message.size <= sizeof(domain) || message.size > sizeof(domain) + 4096 ||
            std::memcmp(message.data, domain, sizeof(domain) - 2) ||
            (message.data[sizeof(domain) - 2] != '1' && message.data[sizeof(domain) - 2] != '2') || message.data[sizeof(domain) - 1] != 0)
            return track_sharing::ManifestVerification::InvalidSignature;
        return ed25519_verify(signature.data, message.data, message.size, key.data) != 0
                   ? track_sharing::ManifestVerification::Valid
                   : track_sharing::ManifestVerification::InvalidSignature;
    }
};
} // namespace platform::esp::storage
