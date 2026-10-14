#pragma once
#include <cstdint>
using TickType_t = uint32_t;
using BaseType_t = int;
inline constexpr int pdTRUE = 1;
inline constexpr int pdFALSE = 0;
#define pdMS_TO_TICKS(value) (value)
