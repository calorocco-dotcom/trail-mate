#include "platform/esp/arduino_common/mesh/esp_preferences_mesh_identity_store.h"
#include "platform/esp/common/radio_identity_record.h"
#include <Curve25519.h>
#include <RNG.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <esp_heap_caps.h>
#include <string>

// The new-account RNG path is deliberately unavailable here. Tests prove that
// bad storage and OOM never invoke generation, using the actual init function.
void RNGClass::begin(const char*) { std::abort(); }
void RNGClass::stir(const uint8_t*, size_t, unsigned int) { std::abort(); }
namespace radio_pki_fixture
{
long random() { std::abort(); }
bool isZeroKey(const uint8_t* value, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        if (value[i]) return false;
    return true;
}
std::string toHex(const uint8_t*, size_t, size_t) { return "fixture"; }
class MtAdapter
{
  public:
    bool initPkiKeys();
    std::array<uint8_t, 32> pki_public_key_{}, pki_private_key_{};
    uint8_t mac_addr_[6]{};
    bool pki_ready_ = false;
};
#define LORA_LOG(...) ((void)0)
#include "radio_pki_init_actual.inc"
#undef LORA_LOG
} // namespace radio_pki_fixture
void testRadioPkiInit()
{
    using namespace radio_pki_fixture;
    fake_nvs::values.clear();
    mesh::LocalIdentity identity{};
    for (size_t i = 0; i < 32; ++i) identity.private_key[i] = uint8_t(i + 1);
    identity.private_key[0] &= 0xf8;
    identity.private_key[31] = (identity.private_key[31] & 0x3f) | 0x40;
    assert(Curve25519::eval(identity.public_key, identity.private_key, nullptr));
    identity.valid = true;
    platform::esp::arduino_common::mesh::EspPreferencesLocalIdentityStore store;
    assert(store.save(identity).ok);
    MtAdapter adapter;
    assert(adapter.initPkiKeys());
    assert(adapter.pki_ready_ && !std::memcmp(adapter.pki_public_key_.data(), identity.public_key, 32));
    auto& bytes = std::get<std::vector<uint8_t>>(fake_nvs::values["chat"][platform::esp::common::kRadioIdentityRecordKey]);
    const auto original = bytes;
    bytes[44] ^= 1;
    const auto invalid = fake_nvs::values;
    assert(!adapter.initPkiKeys());
    assert(!adapter.pki_ready_);
    assert(fake_nvs::values == invalid);
    for (auto byte : adapter.pki_private_key_) assert(!byte);
    bytes = original;
    radio_heap_fixture::fail = true;
    assert(!adapter.initPkiKeys());
    assert(!adapter.pki_ready_);
    radio_heap_fixture::fail = false;
    fake_nvs::open_error = 7;
    assert(!adapter.initPkiKeys());
    assert(!adapter.pki_ready_);
    fake_nvs::open_error = 0;
    assert(adapter.initPkiKeys());
    assert(adapter.pki_ready_);
}
