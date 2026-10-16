#pragma once
#include <cstddef>
#include <cstdint>
// Deliberately NOT cryptography. Tests control storage/dispatch and hash-binding
// branches; SHA-256 interoperability requires the real target crypto backend.
struct mbedtls_sha256_context
{
    uint8_t state[32]{};
    size_t bytes = 0;
};
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
namespace platform::esp::common::crypto
{
inline int sha256_starts(mbedtls_sha256_context*, int) { return 0; }
inline int sha256_update(mbedtls_sha256_context* context, const uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) context->state[(context->bytes++) % 32] ^= bytes[i];
    return 0;
}
inline int sha256_finish(mbedtls_sha256_context* context, uint8_t out[32])
{
    for (size_t i = 0; i < 32; ++i) out[i] = context->state[i];
    return 0;
}
} // namespace platform::esp::common::crypto
