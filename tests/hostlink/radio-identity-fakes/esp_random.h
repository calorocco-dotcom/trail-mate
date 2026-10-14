#pragma once
#include <cstdint>
namespace radio_random_fixture
{
inline unsigned calls = 0;
}
inline uint32_t esp_random()
{
    // Deterministic source only for production initialization control-flow tests.
    // Never evidence of hardware entropy or a real secret-generation guarantee.
    return (++radio_random_fixture::calls * 0x9e3779b9u) ^ 0x7f4a7c15u;
}
