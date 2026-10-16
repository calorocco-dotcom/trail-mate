#pragma once
#include <cstdint>
namespace platform::esp::boards
{
bool lockDisplay(uint32_t);
void unlockDisplay();
} // namespace platform::esp::boards
