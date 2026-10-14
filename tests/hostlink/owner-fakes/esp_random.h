#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
inline void esp_fill_random(void* output,size_t size)
{
    static uint8_t value=20;
    std::memset(output,value++,size);
}
