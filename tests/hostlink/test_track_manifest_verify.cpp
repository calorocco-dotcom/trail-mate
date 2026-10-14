#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/verify_manifest.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
struct Crypto
{
    track_sharing::ManifestVerification verify(geocaching::ByteView key, geocaching::ByteView signature, geocaching::ByteView message) { return ed25519_verify(signature.data, message.data, message.size, key.data) ? track_sharing::ManifestVerification::Valid : track_sharing::ManifestVerification::InvalidSignature; }
};
static uint8_t workspace[4200];
static track_sharing::VerifiedManifestView output;
int main(int argc, char** argv)
{
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    Crypto crypto;
    if (argc != 2)
    {
        assert(track_sharing::verifyManifest({}, crypto, hash, workspace, sizeof(workspace), output) == track_sharing::ManifestVerification::InvalidManifest && !output.verified);
        return 0;
    }
    const std::string hex = argv[1];
    std::vector<uint8_t> raw;
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        unsigned byte = 0;
        if (std::sscanf(hex.substr(i, 2).c_str(), "%2x", &byte) != 1) return 1;
        raw.push_back(uint8_t(byte));
    }
    const auto result = track_sharing::verifyManifest({raw.data(), raw.size()}, crypto, hash, workspace, sizeof(workspace), output);
    if (result != track_sharing::ManifestVerification::Valid)
    {
        assert(!output.verified);
        return 1;
    }
    assert(output.verified);
    for (auto byte : output.object_id) std::printf("%02x", byte);
    std::puts("");
    for (auto byte : output.revision_hash) std::printf("%02x", byte);
    std::puts("");
}
