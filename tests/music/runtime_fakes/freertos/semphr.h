#pragma once
#include "FreeRTOS.h"
#include "scheduler.h"
struct StaticSemaphore_t
{
    bool held = false;
    bool binary = false;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) { return storage; }
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage)
{
    storage->held = false;
    storage->binary = true;
    return storage;
}
inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    if (mutex->binary)
    {
        while (!mutex->held)
        {
            if (!wait) return 0;
            runtime_test::consumer_step();
            assert(!runtime_test::consumer_finished || mutex->held);
        }
        mutex->held = false;
        return pdTRUE;
    }
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
    if (mutex->binary)
    {
        assert(!mutex->held);
        mutex->held = true;
        return;
    }
    assert(mutex->held);
    mutex->held = false;
}
