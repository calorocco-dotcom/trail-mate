#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_identity.h"
#include "platform/esp/arduino_common/mesh/esp_preferences_mesh_identity_store.h"
#include "platform/esp/common/radio_identity_record.h"
#include <Curve25519.h>
#include <array>
#include <cassert>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <vector>
// Only used for non-default Options compatibility. Production default paths
// exercise direct NVS and real crypto, not these vector adapters.
namespace chat::infra
{
bool loadRawBlobFromPreferences(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = 0;
    auto result = nvs_get_blob(handle, key, nullptr, &size);
    if (result == ESP_OK)
    {
        out.resize(size);
        result = nvs_get_blob(handle, key, out.data(), &size);
    }
    nvs_close(handle);
    return result == ESP_OK;
}
bool saveRawBlobToPreferences(const char* ns, const char* key, const uint8_t* value, size_t size)
{
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READWRITE, &handle) != ESP_OK) return false;
    const auto set = nvs_set_blob(handle, key, value, size), commit = set == ESP_OK ? nvs_commit(handle) : set;
    nvs_close(handle);
    return commit == ESP_OK;
}
void clearPreferencesKeys(const char* ns, const char* a, const char* b, const char*)
{
    fake_nvs::values[ns].erase(a);
    if (b) fake_nvs::values[ns].erase(b);
}
} // namespace chat::infra
void testRadioPkiInit();
int main()
{
    using namespace platform::esp::common;
    using chat::meshcore::MeshCoreIdentity;
    std::array<uint8_t, 32> seed{}, pub{};
    std::array<uint8_t, 64> priv{};
    seed.fill(19);
    ed25519_create_keypair(pub.data(), priv.data(), seed.data());
    MeshCoreIdentity imported;
    assert(imported.importPrivateKey(priv.data()));
    MeshCoreIdentity boot;
    assert(boot.init());
    assert(!std::memcmp(boot.publicKey(), pub.data(), 32));
    const auto calls = radio_random_fixture::calls;
    auto& native = std::get<std::vector<uint8_t>>(fake_nvs::values["mc_ident"][kRadioIdentityRecordKey]);
    const auto original = native;
    native[44] ^= 1;
    MeshCoreIdentity corrupt;
    assert(!corrupt.init());
    assert(radio_random_fixture::calls == calls);
    identityRecordPutU32(native.data() + 108, identityRecordCrc(native.data(), 108));
    MeshCoreIdentity wrongPair;
    assert(!wrongPair.init()); // A valid CRC does not authenticate the key pair.
    assert(radio_random_fixture::calls == calls);
    native = original;
    fake_nvs::fail_commit = true;
    seed.fill(21);
    ed25519_create_keypair(pub.data(), priv.data(), seed.data());
    assert(!boot.importPrivateKey(priv.data()));
    assert(!boot.isReady());
    fake_nvs::fail_commit = false;
    MeshCoreIdentity uncertainBoot;
    assert(uncertainBoot.init());
    assert(!std::memcmp(uncertainBoot.publicKey(), pub.data(), 32));
    fake_nvs::values.clear();
    fake_nvs::values["mc_ident"]["priv64"] = std::vector<uint8_t>(priv.begin(), priv.end());
    fake_nvs::values["mc_ident"]["ver"] = uint8_t(1);
    MeshCoreIdentity legacy;
    assert(legacy.init());
    assert(!std::memcmp(legacy.publicKey(), pub.data(), 32));
    assert(fake_nvs::values["mc_ident"].count(kRadioIdentityRecordKey));
    assert(radio_random_fixture::calls == calls);
    fake_nvs::values.clear();
    fake_nvs::values["mc_ident"]["pub32"] = std::vector<uint8_t>(32, 3);
    MeshCoreIdentity partial;
    assert(!partial.init());
    assert(radio_random_fixture::calls == calls);
    fake_nvs::values.clear();
    MeshCoreIdentity fresh;
    assert(fresh.init());
    assert(radio_random_fixture::calls > calls);

    using Store = platform::esp::arduino_common::mesh::EspPreferencesLocalIdentityStore;
    Store store;
    mesh::LocalIdentity identity{}, loaded{};
    for (size_t i = 0; i < 32; ++i) identity.private_key[i] = uint8_t(i + 1);
    identity.private_key[0] &= 0xf8;
    identity.private_key[31] = (identity.private_key[31] & 0x3f) | 0x40;
    assert(Curve25519::eval(identity.public_key, identity.private_key, nullptr));
    identity.valid = true;
    assert(store.save(identity).ok);
    assert(store.load(loaded).ok);
    assert(!std::memcmp(identity.private_key, loaded.private_key, 32));
    const auto before = fake_nvs::values;
    identity.public_key[0] ^= 1;
    assert(!store.save(identity).ok);
    assert(fake_nvs::values == before);
    identity.public_key[0] ^= 1;
    radio_heap_fixture::fail = true;
    const auto attempts = radio_heap_fixture::attempts;
    assert(!store.save(identity).ok);
    assert(!store.load(loaded).ok);
    assert(fake_nvs::values == before);
    assert(radio_heap_fixture::attempts == attempts + 2);
    assert(radio_heap_fixture::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); // No internal retry.
    radio_heap_fixture::fail = false;
    fake_nvs::values["chat"]["pki_pub"] = std::vector<uint8_t>(identity.public_key, identity.public_key + 32);
    fake_nvs::values["chat"]["pki_priv"] = std::vector<uint8_t>(identity.private_key, identity.private_key + 32);
    assert(store.clear().ok);
    assert(store.load(loaded).failure == mesh::StoreFailure::NotFound);
    assert(fake_nvs::values["chat"].count("pki_priv")); // Tombstone blocks resurrection.
    assert(store.save(identity).ok);
    assert(store.load(loaded).ok);
    fake_nvs::values["chat"].erase(kRadioIdentityRecordKey);
    assert(store.load(loaded).ok);
    auto& legacy_public = std::get<std::vector<uint8_t>>(fake_nvs::values["chat"]["pki_pub"]);
    legacy_public[0] ^= 1;
    assert(store.load(loaded).failure == mesh::StoreFailure::Corrupt);
    assert(!loaded.valid);
    fake_nvs::values["chat"]["pki_pub"] = uint8_t(1);
    assert(store.load(loaded).failure == mesh::StoreFailure::Corrupt);
    assert(!loaded.valid);
    fake_nvs::values["chat"].erase("pki_pub");
    assert(store.load(loaded).failure == mesh::StoreFailure::Corrupt);
    fake_nvs::open_error = 7;
    assert(store.load(loaded).failure == mesh::StoreFailure::IoError);
    fake_nvs::open_error = 0;
    Store::Options options;
    options.public_key = "custom_pub";
    options.private_key = "custom_priv";
    Store custom(options);
    assert(custom.save(identity).ok);
    assert(custom.load(loaded).ok);
    assert(fake_nvs::values["chat"].count("custom_priv"));
    assert(!fake_nvs::values["chat"].count(kRadioIdentityRecordKey));
    assert(fake_nvs::handles.empty());
    radio_heap_fixture::external_size = 0;
    assert(custom.load(loaded).ok);
    assert(radio_heap_fixture::last_caps == MALLOC_CAP_8BIT);
    assert(radio_heap_fixture::allocations == radio_heap_fixture::frees && radio_heap_fixture::blocks.empty());
    radio_heap_fixture::external_size = 8 * 1024 * 1024;
    testRadioPkiInit();
    assert(radio_heap_fixture::allocations == radio_heap_fixture::frees && radio_heap_fixture::blocks.empty());
}
