#pragma once
// Native verification uses the real SDK Mbed TLS implementation with a
// minimal software SHA-256 configuration, not the fake dispatch hash.
#define MBEDTLS_SHA256_C
#define MBEDTLS_PLATFORM_C
#if defined(_WIN32)
#define MBEDTLS_PLATFORM_ZEROIZE_ALT
#endif
