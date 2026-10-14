#pragma once

#include <cstdint>

#if defined(ARDUINO)

#include <Curve25519.h>
#include <RNG.h>

#else

class Curve25519
{
  public:
    static void dh1(std::uint8_t public_key[32], std::uint8_t private_key[32]);
    static bool dh2(std::uint8_t peer_key_and_secret[32], std::uint8_t private_key[32]);
    static bool eval(std::uint8_t output[32], const std::uint8_t private_key[32], const std::uint8_t point[32]);
};

class ReticulumRngCompat
{
  public:
    void begin(const char* personalization) const;
};

extern ReticulumRngCompat RNG;

#endif
