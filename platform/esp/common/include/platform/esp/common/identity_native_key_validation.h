#pragma once
#include "hostlink/identity_signing_key_validation.h"
#include "platform/esp/common/reticulum_crypto_compat.h"

namespace platform::esp::common
{
// Caller provides one 32-byte work area. The existing curve implementation has
// its own arithmetic stack usage; invoke on the identity-management owner task.
inline bool validateNativeIdentityKeys(const uint8_t* public_key, const uint8_t* private_key,
                                       uint8_t* scratch, size_t capacity)
{
    if (!public_key || !private_key || !scratch || capacity < 32) return false;
    const uintptr_t work = reinterpret_cast<uintptr_t>(scratch);
    const uintptr_t pub = reinterpret_cast<uintptr_t>(public_key);
    const uintptr_t priv = reinterpret_cast<uintptr_t>(private_key);
    if ((work >= pub ? work - pub < 64 : pub - work < 32) ||
        (work >= priv ? work - priv < 96 : priv - work < 32)) return false;
    bool valid = hostlink::validateExpandedSigningKey(public_key + 32, private_key + 32, scratch, capacity);
    uint8_t nonzero = 0;
    for (size_t i = 0; i < 32; ++i) nonzero |= private_key[i];
    // Native Arduino storage comes from dh1(), which clamps before eval().
    // Do not silently reinterpret a seed-format key as native expanded data.
    const bool canonical = (private_key[0] & 7) == 0 && (private_key[31] & 0xc0) == 0x40;
    if (valid && nonzero && canonical)
    {
        valid = Curve25519::eval(scratch, private_key, nullptr);
        uint8_t difference = 0;
        for (size_t i = 0; i < 32; ++i) difference |= scratch[i] ^ public_key[i];
        valid = valid && difference == 0;
    }
    else valid = false;
    volatile uint8_t* clear = scratch;
    for (size_t i = 0; i < 32; ++i) clear[i] = 0;
    return valid;
}
} // namespace platform::esp::common
