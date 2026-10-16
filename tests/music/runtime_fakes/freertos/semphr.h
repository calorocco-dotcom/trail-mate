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
