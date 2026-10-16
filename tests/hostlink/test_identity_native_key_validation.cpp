#include "platform/esp/common/identity_native_key_validation.h"
#include "platform/esp/common/identity_seed_key_conversion.h"
#include <array>
#include <cassert>
#include <cstring>

void testIdentityStoredKeyValidation();
void testIdentityRestoreActual();
int main()
{
    testIdentityStoredKeyValidation();
    testIdentityRestoreActual();
    std::array<uint8_t, 64> public_key{};
    std::array<uint8_t, 96> private_key{};
    std::array<uint8_t, 32> seed{}, scratch{};
    for (size_t i = 0; i < 32; ++i) private_key[i] = static_cast<uint8_t>(i + 1);
    private_key[0] &= 0xf8;
    private_key[31] = (private_key[31] & 0x7f) | 0x40;
    // Independently derived with Python cryptography X25519, not this backend.
    const uint8_t expected[] = {0x07, 0xa3, 0x7c, 0xbc, 0x14, 0x20, 0x93, 0xc8, 0xb7, 0x55, 0xdc, 0x1b, 0x10, 0xe8, 0x6c, 0xb4, 0x26, 0x37, 0x4a, 0xd1, 0x6a, 0xa8, 0x53, 0xed, 0x0b, 0xdf, 0xc0, 0xb2, 0xb8, 0x6d, 0x1c, 0x7c};
    std::memcpy(public_key.data(), expected, 32);
    seed.fill(19);
    ed25519_create_keypair(public_key.data() + 32, private_key.data() + 32, seed.data());
    std::array<uint8_t, 64> seeds{};
    std::array<uint8_t, 96> converted{};
    for (size_t i = 0; i < 32; ++i) seeds[i] = uint8_t(i + 1);
    std::memcpy(seeds.data() + 32, seed.data(), 32);
    const auto original_seeds = seeds;
    assert(platform::esp::common::convertSeedIdentityKeys(public_key.data(), seeds.data(), converted.data(), converted.size(), scratch.data(), scratch.size()));
    assert(converted == private_key && seeds == original_seeds);
    for (auto byte : scratch) assert(byte == 0);
    for (const size_t index : {size_t(0), size_t(32)})
    {
        public_key[index] ^= 1;
        assert(!platform::esp::common::convertSeedIdentityKeys(public_key.data(), seeds.data(), converted.data(), converted.size(), scratch.data(), scratch.size()));
        for (auto byte : converted) assert(byte == 0);
        public_key[index] ^= 1;
    }
    assert(!platform::esp::common::convertSeedIdentityKeys(public_key.data(), seeds.data(), seeds.data(), 96, scratch.data(), scratch.size()));
    assert(seeds == original_seeds);
    assert(platform::esp::common::validateNativeIdentityKeys(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
    for (auto byte : scratch) assert(byte == 0);
    private_key[11] ^= 1;
    assert(!platform::esp::common::validateNativeIdentityKeys(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
    private_key[11] ^= 1;
    public_key[3] ^= 1;
    assert(!platform::esp::common::validateNativeIdentityKeys(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
    public_key[3] ^= 1;
    private_key[44] ^= 1;
    assert(!platform::esp::common::validateNativeIdentityKeys(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
    const auto saved = private_key;
    assert(!platform::esp::common::validateNativeIdentityKeys(public_key.data(), private_key.data(), private_key.data() + 80, 32));
    assert(private_key == saved);
    assert(!platform::esp::common::validateNativeIdentityKeys(public_key.data(), private_key.data(), scratch.data(), 31));
}
