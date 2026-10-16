#pragma once
#include "host_worker.h"
inline int64_t esp_timer_get_time() { return ++host_worker::ticks * 100; }
