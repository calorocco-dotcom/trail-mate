#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/verify_read_manifest.h"
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
    track_sharing::ManifestVerification verify(geocaching::ByteView key, geocaching::ByteView signature, geocaching::ByteView message)
    {
        return ed25519_verify(signature.data, message.data, message.size, key.data) ? track_sharing::ManifestVerification::Valid : track_sharing::ManifestVerification::InvalidSignature;
    }
};
static uint8_t workspace[4200];
static mbedtls_sha256_context context;
static track_sharing::VerifiedManifestView output;
std::vector<uint8_t> hex(const char* input)
{
    std::string text = input;
    std::vector<uint8_t> result;
    if (text.size() % 2) return {};
    for (size_t i = 0; i < text.size(); i += 2)
    {
        unsigned value = 0;
        if (std::sscanf(text.substr(i, 2).c_str(), "%2x", &value) != 1) return {};
        result.push_back(uint8_t(value));
    }
    return result;
}
int main(int argc, char** argv)
{
    Crypto crypto;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    if (argc != 5)
    {
        assert(track_sharing::verifyReadManifest({}, {}, {}, {}, crypto, hash, workspace, sizeof(workspace), output) != track_sharing::ReadManifestVerification::Valid && !output.verified);
        return 0;
    }
    const auto raw = hex(argv[1]), request = hex(argv[2]), object = hex(argv[3]), revision = hex(argv[4]);
    const auto result = track_sharing::verifyReadManifest({raw.data(), raw.size()}, {request.data(), request.size()}, {object.data(), object.size()}, {revision.data(), revision.size()}, crypto, hash, workspace, sizeof(workspace), output);
    if (result != track_sharing::ReadManifestVerification::Valid)
    {
        assert(!output.verified);
        return 1;
    }
    assert(output.verified);
    return 0;
}
