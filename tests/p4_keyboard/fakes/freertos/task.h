#pragma once
#include <vector>
namespace fake
{
inline std::vector<unsigned> delays;
}
inline void vTaskDelay(unsigned ticks) { fake::delays.push_back(ticks); }
