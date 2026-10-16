#pragma once
#include <cassert>
#include <cstdint>
using TickType_t = uint32_t;
using UBaseType_t = unsigned;
constexpr int pdTRUE = 1, pdPASS = 1, tskIDLE_PRIORITY = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(value) (value)
#define pdTICKS_TO_MS(value) (value)
namespace runtime_test
{
inline TickType_t tick = 1;
inline void (*scheduled)(void*) = nullptr;
inline void* task_context = nullptr;
inline void (*on_delay)() = nullptr;
inline unsigned task_starts = 0;
inline unsigned delays = 0, yields = 0;
inline UBaseType_t priority = 1;
inline int pinned_core = -1;
inline TickType_t ui_busy_until = 0;
inline unsigned busy_skips = 0, playback_lock_waits = 0;
} // namespace runtime_test
