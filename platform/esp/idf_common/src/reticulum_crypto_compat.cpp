#include "platform/esp/common/reticulum_crypto_compat.h"

#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include "esp_random.h"
#include "mbedtls/ecp.h"

#include <cstring>

namespace
{

constexpr std::size_t kX25519KeySize = 32;

int fill_random(void*, unsigned char* out, std::size_t len)
{
    if (!out && len != 0)
    {
        return -1;
    }
    esp_fill_random(out, len);
    return 0;
}

bool all_zero(const std::uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return true;
    }

    std::uint8_t combined = 0;
    for (std::size_t index = 0; index < len; ++index)
    {
        combined |= data[index];
    }
    return combined == 0;
}

bool multiply_x25519(const std::uint8_t scalar[kX25519KeySize],
                     const std::uint8_t point[kX25519KeySize],
                     std::uint8_t out[kX25519KeySize])
{
    if (!scalar || !point || !out)
    {
        return false;
    }

    mbedtls_ecp_group group;
    mbedtls_ecp_point input;
    mbedtls_ecp_point result;
    mbedtls_mpi private_scalar;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&input);
    mbedtls_ecp_point_init(&result);
    mbedtls_mpi_init(&private_scalar);

    int status = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519);
    if (status == 0)
    {
        status = mbedtls_mpi_read_binary_le(&private_scalar, scalar, kX25519KeySize);
    }
    if (status == 0)
    {
        status = mbedtls_mpi_read_binary_le(&input.MBEDTLS_PRIVATE(X),
                                            point,
                                            kX25519KeySize);
    }
    if (status == 0)
    {
        status = mbedtls_mpi_lset(&input.MBEDTLS_PRIVATE(Z), 1);
    }
    if (status == 0)
    {
        status = mbedtls_ecp_mul(&group,
                                 &result,
                                 &private_scalar,
                                 &input,
                                 fill_random,
                                 nullptr);
    }
    if (status == 0)
    {
        status = mbedtls_mpi_write_binary_le(&result.MBEDTLS_PRIVATE(X),
                                             out,
                                             kX25519KeySize);
    }

    mbedtls_mpi_free(&private_scalar);
    mbedtls_ecp_point_free(&result);
    mbedtls_ecp_point_free(&input);
    mbedtls_ecp_group_free(&group);
    return status == 0;
}

} // namespace

ReticulumRngCompat RNG{};

bool Curve25519::eval(std::uint8_t output[32], const std::uint8_t private_key[32], const std::uint8_t point[32])
{
    static constexpr std::uint8_t base_point[kX25519KeySize] = {9};
    if (!output || !private_key) return false;
    const bool ok = multiply_x25519(private_key, point ? point : base_point, output) && !all_zero(output, kX25519KeySize);
    if (!ok) std::memset(output, 0, kX25519KeySize);
    return ok;
}

void ReticulumRngCompat::begin(const char*) const
{
    // ESP-IDF entropy is provided directly by esp_fill_random().
}

void Curve25519::dh1(std::uint8_t public_key[32], std::uint8_t private_key[32])
{
    if (!public_key || !private_key)
    {
        return;
    }

    const std::uint8_t base_point[kX25519KeySize] = {9};
    for (std::size_t attempt = 0; attempt < 16; ++attempt)
    {
        esp_fill_random(private_key, kX25519KeySize);
        private_key[0] &= 0xF8U;
        private_key[31] = static_cast<std::uint8_t>((private_key[31] & 0x7FU) | 0x40U);
        if (multiply_x25519(private_key, base_point, public_key) &&
            !all_zero(public_key, kX25519KeySize))
        {
            return;
        }
    }

    std::memset(public_key, 0, kX25519KeySize);
    std::memset(private_key, 0, kX25519KeySize);
}

bool Curve25519::dh2(std::uint8_t peer_key_and_secret[32], std::uint8_t private_key[32])
{
    if (!peer_key_and_secret || !private_key)
    {
        return false;
    }

    std::uint8_t peer_key[kX25519KeySize] = {};
    std::memcpy(peer_key, peer_key_and_secret, sizeof(peer_key));
    const bool ok = !all_zero(peer_key, sizeof(peer_key)) &&
                    multiply_x25519(private_key, peer_key, peer_key_and_secret) &&
                    !all_zero(peer_key_and_secret, kX25519KeySize);
    std::memset(peer_key, 0, sizeof(peer_key));
    std::memset(private_key, 0, kX25519KeySize);
    if (!ok)
    {
        std::memset(peer_key_and_secret, 0, kX25519KeySize);
    }
    return ok;
}

#endif
