#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "geocaching/protocol/cmp_writer.h"
#include "track_sharing/device_signer.h"
#include "track_sharing/sign_manifest.h"
#include <cassert>
#include <cstdio>
struct Identity
{
    uint8_t pub[64]{}, secret[64]{};
    uint32_t generation = 7;
    unsigned calls = 0;
    bool change_generation = false;
    uint32_t authorGeneration() const { return generation; }
    bool ready = true, storage_error = false;
    bool isReady() const { return ready; }
    bool identityStorageError() const { return storage_error; }
    void combinedPublicKey(uint8_t* output) { std::memcpy(output, pub, 64); }
    bool sign(const uint8_t* message, size_t length, uint8_t* signature)
    {
        ed25519_sign(signature, message, length, pub + 32, secret);
        ++calls;
        if (change_generation) ++generation;
        return true;
    }
};
static uint8_t raw[4096], workspace[4200], signature[64];
int main(int argc, char** argv)
{
    const unsigned version = argc == 2 && std::strcmp(argv[1], "2") == 0 ? 2 : 1;
    Identity identity;
    uint8_t seed[32]{}, nonce[16] = {1}, hash[32] = {2};
    for (unsigned i = 0; i < 32; ++i)
    {
        seed[i] = uint8_t(i);
        identity.pub[i] = 3;
    }
    ed25519_create_keypair(identity.pub + 32, identity.secret, seed);
    geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
    assert(writer.array(version == 1 ? 14 : 16) && writer.unsignedInteger(version) && writer.binary({identity.pub, 64}) && writer.binary({nonce, 16}) && writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.nil() && writer.unsignedInteger(0) && writer.unsignedInteger(5000000000ULL) && writer.unsignedInteger(5000000001ULL) && writer.text("山径") && writer.text("原生签名"));
    assert(writer.array(4));
    for (unsigned i = 0; i < 4; ++i) assert(writer.unsignedInteger(0));
    assert(writer.array(4) && writer.unsignedInteger(123) && writer.nil() && writer.nil() && writer.unsignedInteger(2) && writer.array(4) && writer.binary({hash, 32}) && writer.unsignedInteger(300) && writer.text("application/vnd.google-earth.kml+xml") && writer.unsignedInteger(1));
    if (version == 2)
        assert(writer.array(8) && writer.unsignedInteger(1) && writer.array(1) && writer.text("hike") && writer.unsignedInteger(2) && writer.unsignedInteger(1) && writer.unsignedInteger(0) && writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.nil() && writer.nil());
    track_sharing::ManifestView view;
    const geocaching::ByteView bytes{raw, writer.size()};
    assert(!track_sharing::signManifest(bytes, identity, view, raw, sizeof(raw), signature, sizeof(signature)));
    assert(!track_sharing::signManifest(bytes, identity, view, workspace, 1, signature, sizeof(signature)));
    raw[4] ^= 1;
    assert(!track_sharing::signManifest(bytes, identity, view, workspace, sizeof(workspace), signature, sizeof(signature)));
    raw[4] ^= 1;
    assert(track_sharing::signManifest(bytes, identity, view, workspace, sizeof(workspace), signature, sizeof(signature)));
    const auto calls = identity.calls;
    assert(!track_sharing::signManifestForGeneration(bytes, identity, 8, view, workspace, sizeof(workspace), signature, sizeof(signature)) && identity.calls == calls);
    for (auto byte : signature) assert(!byte);
    identity.change_generation = true;
    assert(!track_sharing::signManifestForGeneration(bytes, identity, 7, view, workspace, sizeof(workspace), signature, sizeof(signature)) && identity.generation == 8);
    for (auto byte : signature) assert(!byte);
    identity.change_generation = false;
    identity.generation = 7;
    assert(track_sharing::signManifestForGeneration(bytes, identity, 7, view, workspace, sizeof(workspace), signature, sizeof(signature)));
    using Result = chat::DeviceAuthorResult;
    identity.ready = false;
    assert(track_sharing::signDeviceTrackManifest(identity, raw, bytes.size, 7, view, workspace, sizeof(workspace), signature, sizeof(signature)) == Result::Unavailable);
    identity.storage_error = true;
    assert(track_sharing::signDeviceTrackManifest(identity, raw, bytes.size, 7, view, workspace, sizeof(workspace), signature, sizeof(signature)) == Result::StorageError);
    identity.ready = true;
    identity.storage_error = false;
    identity.generation = 0;
    assert(track_sharing::signDeviceTrackManifest(identity, raw, bytes.size, 7, view, workspace, sizeof(workspace), signature, sizeof(signature)) == Result::StorageError);
    identity.generation = 8;
    assert(track_sharing::signDeviceTrackManifest(identity, raw, bytes.size, 7, view, workspace, sizeof(workspace), signature, sizeof(signature)) == Result::StaleIdentity);
    for (auto byte : signature) assert(!byte);
    identity.generation = 7;
    assert(track_sharing::signDeviceTrackManifest(identity, raw, bytes.size, 7, view, workspace, sizeof(workspace), signature, sizeof(signature)) == Result::Ok);
    assert(ed25519_verify(signature, workspace, sizeof("TrailMate.Track.Manifest.v1") + bytes.size, identity.pub + 32));
    assert(view.version == version && workspace[sizeof("TrailMate.Track.Manifest.v1") - 2] == (version == 2 ? '2' : '1'));
    for (size_t i = 0; i < bytes.size; ++i) std::printf("%02x", raw[i]);
    std::puts("");
    for (auto byte : signature) std::printf("%02x", byte);
    std::puts("");
}
