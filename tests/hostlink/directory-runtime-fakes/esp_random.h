#pragma once
#include <cstdint>
#include <cstring>
inline void esp_fill_random(void* output, size_t bytes)
{
    static uint8_t value = 1;
    std::memset(output, value++, bytes);
}
