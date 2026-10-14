#pragma once
#include <cstdint>
inline int64_t directory_clock_us = 1000000;
inline int64_t esp_timer_get_time() { return directory_clock_us; }
