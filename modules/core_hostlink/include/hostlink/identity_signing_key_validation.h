#pragma once
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include <cstddef>
#include <cstdint>

namespace hostlink
{
// Expanded Ed25519 private keys contain a scalar and nonce prefix, not a seed.
// Validate the scalar's public key directly; no seed recovery or key generation.
// The nonce prefix is secret signing material and has no public-key equivalent.
inline bool validateExpandedSigningKey(const uint8_t* public_key, const uint8_t* private_key,
                                       uint8_t* scratch, size_t capacity)
{
    if (!public_key || !private_key || !scratch || capacity < 32) return false;
    const uintptr_t work = reinterpret_cast<uintptr_t>(scratch);
    const uintptr_t pub = reinterpret_cast<uintptr_t>(public_key);
    const uintptr_t priv = reinterpret_cast<uintptr_t>(private_key);
    if ((work >= pub ? work - pub < 32 : pub - work < 32) ||
        (work >= priv ? work - priv < 64 : priv - work < 32)) return false;
    uint8_t nonzero = 0;
    for (size_t i = 0; i < 32; ++i) nonzero |= private_key[i];
    bool valid = nonzero != 0;
    if (valid)
    {
        ed25519_derive_pub(scratch, private_key);
        uint8_t difference = 0;
        for (size_t i = 0; i < 32; ++i) difference |= scratch[i] ^ public_key[i];
        valid = difference == 0;
    }
    volatile uint8_t* clear = scratch;
    for (size_t i = 0; i < 32; ++i) clear[i] = 0;
    return valid;
}
} // namespace hostlink
