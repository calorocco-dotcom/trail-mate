#include "hostlink/identity_signing_key_validation.h"
#include <array>
#include <cassert>

int main()
{
    std::array<uint8_t, 32> seed{}, public_key{};
    std::array<uint8_t, 64> private_key{};
    std::array<uint8_t, 40> scratch{};
    seed.fill(17);
    ed25519_create_keypair(public_key.data(), private_key.data(), seed.data());
    scratch.fill(99);
    assert(hostlink::validateExpandedSigningKey(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
    for (size_t i = 0; i < 32; ++i) assert(scratch[i] == 0);
    assert(scratch[32] == 99);
    private_key[12] ^= 1;
    assert(!hostlink::validateExpandedSigningKey(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
    private_key[12] ^= 1;
    public_key[7] ^= 1;
    assert(!hostlink::validateExpandedSigningKey(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
    public_key[7] ^= 1;
    const auto original = private_key;
    assert(!hostlink::validateExpandedSigningKey(public_key.data(), private_key.data(), private_key.data(), private_key.size()));
    assert(private_key == original);
    assert(!hostlink::validateExpandedSigningKey(public_key.data(), private_key.data(), scratch.data(), 31));
    private_key.fill(0);
    assert(!hostlink::validateExpandedSigningKey(public_key.data(), private_key.data(), scratch.data(), scratch.size()));
}
