#pragma once
#include "freertos/FreeRTOS.h"
#include <atomic>
#include <cassert>
struct TestMutex
{
    std::atomic_flag held = ATOMIC_FLAG_INIT;
};
using SemaphoreHandle_t = TestMutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new TestMutex; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t)
{
    return mutex && !mutex->held.test_and_set() ? pdTRUE : pdFALSE;
}
inline int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex && mutex->held.test_and_set());
    mutex->held.clear();
    return pdTRUE;
}
