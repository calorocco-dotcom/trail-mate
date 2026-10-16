#include "platform/esp/common/config_preferences.h"
#include "platform/esp/common/identity_generation_recovery.h"
#include "platform/esp/common/identity_native_key_validation.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace
{
constexpr const char* kPrefsNs = "lxmf_ident";
constexpr const char* kEncPubKey = "enc_pub";
constexpr const char* kEncPrivKey = "enc_priv";
constexpr const char* kSigPubKey = "sig_pub";
constexpr const char* kSigPrivKey = "sig_priv";
bool isAllZero(const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (data[i]) return false;
    return true;
}
class LxmfIdentity
{
  public:
    static constexpr size_t kSigPubKeySize = 32;
    std::array<uint8_t, 32> enc_pub_{}, enc_priv_{}, sig_pub_{};
    std::array<uint8_t, 64> sig_priv_{};
    unsigned recomputed = 0;
    uint32_t author_generation_ = 0;
    std::array<uint8_t, 16> identity_hash_{};
    bool loadFromPrefs();
    void recomputeDerivedFields()
    {
        ++recomputed;
        identity_hash_.fill(5);
    }
};
// Exact production loader; only derived-address calculation and NVS I/O are fixtures.
#include "identity_load_actual.inc"
} // namespace
void testIdentityStoredKeyValidation()
{
    LxmfIdentity source;
    source.enc_priv_.fill(24);
    source.enc_priv_[31] = 0x58;
    assert(Curve25519::eval(source.enc_pub_.data(), source.enc_priv_.data(), nullptr));
    std::array<uint8_t, 32> seed{};
    seed.fill(3);
    ed25519_create_keypair(source.sig_pub_.data(), source.sig_priv_.data(), seed.data());
    auto& storage = fake_nvs::values[kPrefsNs];
    storage[kEncPubKey] = std::vector<uint8_t>(source.enc_pub_.begin(), source.enc_pub_.end());
    storage[kEncPrivKey] = std::vector<uint8_t>(source.enc_priv_.begin(), source.enc_priv_.end());
    storage[kSigPubKey] = std::vector<uint8_t>(source.sig_pub_.begin(), source.sig_pub_.end());
    storage[kSigPrivKey] = std::vector<uint8_t>(source.sig_priv_.begin(), source.sig_priv_.end());
    const auto original = storage;
    LxmfIdentity valid;
    assert(valid.loadFromPrefs() && valid.recomputed == 1);
    for (const char* key : {kEncPubKey, kEncPrivKey, kSigPubKey, kSigPrivKey})
    {
        storage = original;
        std::get<std::vector<uint8_t>>(storage[key])[11] ^= 1;
        const auto damaged = storage;
        LxmfIdentity invalid;
        assert(!invalid.loadFromPrefs() && invalid.recomputed == 0);
        assert(storage == damaged);
    }
    storage = original;
    std::get<std::vector<uint8_t>>(storage[kEncPrivKey])[0] |= 1;
    LxmfIdentity unclamped;
    assert(!unclamped.loadFromPrefs());
    storage = original;
    std::array<uint8_t, 64> combined_pub{};
    std::array<uint8_t, 96> combined_priv{};
    std::array<uint8_t, platform::esp::common::kIdentityRecordWorkBytes> work{};
    std::memcpy(combined_pub.data(), source.enc_pub_.data(), 32);
    std::memcpy(combined_pub.data() + 32, source.sig_pub_.data(), 32);
    std::memcpy(combined_priv.data(), source.enc_priv_.data(), 32);
    std::memcpy(combined_priv.data() + 32, source.sig_priv_.data(), 64);
    using namespace platform::esp::common;
    assert(commitIdentityRecord(5, 5, combined_pub.data(), combined_priv.data(), work.data(), work.size(), validateNativeIdentityKeys) == IdentityRecordResult::Committed);
    // New record is authoritative even when legacy encryption keys are damaged.
    std::get<std::vector<uint8_t>>(storage[kEncPrivKey])[11] ^= 1;
    LxmfIdentity restored;
    assert(restored.loadFromPrefs() && restored.author_generation_ == 6 && restored.enc_priv_ == source.enc_priv_);
    const auto record = storage["native_v1"];
    std::get<std::vector<uint8_t>>(storage["native_v1"])[8] ^= 1;
    LxmfIdentity corrupt_record;
    assert(!corrupt_record.loadFromPrefs() && corrupt_record.recomputed == 0);
    storage["native_v1"] = record;
    auto& epoch = std::get<std::vector<uint8_t>>(fake_nvs::values["tm_author"]["epoch"]);
    epoch[4] ^= 1;
    LxmfIdentity collision;
    assert(!collision.loadFromPrefs() && collision.author_generation_ == 0);
    assert(fake_nvs::handles.empty());
}
