#include "geocaching/protocol/cmp_reader.h"
#include "hostlink/identity_backup_export.h"
#include <array>
#include <cassert>
struct Identity
{
    bool ready = true, stale_on_sign = false;
    mutable uint32_t generation = 7;
    std::array<uint8_t, 16> hash{}, delivery{};
    bool isReady() const { return ready; }
    bool identityStorageError() const { return false; }
    uint32_t authorGeneration() const { return generation; }
    void combinedPublicKey(uint8_t* output) const { std::memset(output, 3, 64); }
    bool copyNativePrivateKey(uint32_t expected, uint8_t* output, size_t capacity) const
    {
        if (expected != generation || capacity < 96) return false;
        std::memset(output, 4, 32);
        std::memset(output + 32, 5, 64);
        return true;
    }
    const uint8_t* identityHash() const { return hash.data(); }
    const uint8_t* destinationHash() const { return delivery.data(); }
    bool sign(const uint8_t* message, size_t size, uint8_t* signature) const
    {
        assert(size > 96 && !std::memcmp(message, "TrailMate.Identity.Backup.v1", 26));
        std::memset(signature, 6, 64);
        if (stale_on_sign) ++generation;
        return true;
    }
};
int main()
{
    Identity identity;
    std::array<uint8_t, 448> output{};
    size_t written = 0;
    assert(hostlink::management::exportNativeIdentityBackup(identity, 7, output.data(), output.size(), written) == chat::DeviceAuthorResult::Ok);
    geocaching::protocol::CmpReader outer({output.data(), written});
    size_t count = 0;
    geocaching::ByteView payload, signature;
    assert(outer.array(count, 2) && count == 2 && outer.binary(payload, 320) && outer.binary(signature, 64) && signature.size == 64 && outer.finished());
    geocaching::protocol::CmpReader reader(payload);
    uint64_t value = 0;
    std::string_view type;
    geocaching::ByteView key, secret, hash, delivery;
    assert(reader.array(count, 8) && count == 8 && reader.unsignedInteger(value) && value == 1 && reader.text(type, 40) && type == "trailmate.identity.backup.v1" && reader.unsignedInteger(value) && value == 0);
    assert(reader.binary(key, 64) && key.size == 64 && reader.binary(secret, 96) && secret.size == 96 && secret.data[0] == 4 && secret.data[32] == 5);
    assert(reader.binary(hash, 16) && reader.binary(delivery, 16) && reader.unsignedInteger(value) && value == 7 && reader.finished());
    for (size_t i = written; i < output.size(); ++i) assert(output[i] == 0);
    identity.stale_on_sign = true;
    assert(hostlink::management::exportNativeIdentityBackup(identity, 7, output.data(), output.size(), written) == chat::DeviceAuthorResult::StaleIdentity && written == 0);
    for (auto byte : output) assert(byte == 0);
    identity.ready = false;
    assert(hostlink::management::exportNativeIdentityBackup(identity, 8, output.data(), output.size(), written) == chat::DeviceAuthorResult::Unavailable);
}
