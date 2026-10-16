#pragma once
#include <cstddef>
#include <cstdint>
namespace hostlink
{
// Volatile writes survive optimization. Own and exclusively lend this buffer;
// this wipes memory only, not copies already delivered to the USB host.
inline void clearSensitiveBytes(uint8_t* data, size_t length)
{
    volatile uint8_t* destination = data;
    if (!destination) return;
    while (length--) *destination++ = 0;
}
} // namespace hostlink
