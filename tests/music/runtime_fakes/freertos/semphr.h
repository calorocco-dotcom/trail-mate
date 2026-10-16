#pragma once
#include "FreeRTOS.h"
struct StaticSemaphore_t
{
    bool held = false;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) { return storage; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    if (runtime_test::priority > 1 && runtime_test::tick < runtime_test::ui_busy_until)
    {
        if (wait == 0)
        {
            ++runtime_test::busy_skips;
            return 0;
        }
        ++runtime_test::playback_lock_waits;
        runtime_test::tick = runtime_test::ui_busy_until;
    }
    if (mutex->held)
    {
        assert(wait != portMAX_DELAY);
        return 0;
    }
    mutex->held = true;
    return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex->held);
    mutex->held = false;
}
