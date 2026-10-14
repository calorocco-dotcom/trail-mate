#pragma once
#include "platform/esp/common/reticulum_crypto_compat.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace platform::esp::common
{
// Reticulum input is X25519 private32 + Ed25519 seed32. Native output is
// clamped X25519 private32 + expanded Ed25519 private64. All arenas are caller
// owned; restore can reuse its existing PSRAM work after signature verification.
inline bool convertSeedIdentityKeys(const uint8_t* public_key, const uint8_t* seeds,
                                    uint8_t* native, size_t native_capacity,
                                    uint8_t* scratch, size_t scratch_capacity)
{
    if (!public_key || !seeds || !native || native_capacity < 96 || !scratch || scratch_capacity < 32) return false;
    const auto overlap=[](const void* a,size_t na,const void* b,size_t nb){const auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);return x<=y?y-x<na:x-y<nb;};
    if (overlap(native,96,public_key,64)||overlap(native,96,seeds,64)||overlap(scratch,32,public_key,64)||overlap(scratch,32,seeds,64)||overlap(native,96,scratch,32)) return false;
    uint8_t enc_nonzero=0,sig_nonzero=0;
    for(size_t i=0;i<32;++i){enc_nonzero|=seeds[i];sig_nonzero|=seeds[32+i];}
    bool valid=enc_nonzero&&sig_nonzero;
    if(valid){
        std::memcpy(native,seeds,32);native[0]&=0xf8;native[31]=(native[31]&0x7f)|0x40;
        ed25519_create_keypair(scratch,native+32,seeds+32);
        uint8_t difference=0;
        for(size_t i=0;i<32;++i)difference|=scratch[i]^public_key[32+i];
        valid=difference==0&&Curve25519::eval(scratch,native,nullptr);
        if(valid){for(size_t i=0;i<32;++i)difference|=scratch[i]^public_key[i];valid=difference==0;}
    }
    volatile uint8_t* clear=scratch;for(size_t i=0;i<32;++i)clear[i]=0;
    if(!valid){clear=native;for(size_t i=0;i<96;++i)clear[i]=0;}
    return valid;
}
} // namespace platform::esp::common
