#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void*;
inline int xTaskCreate(void (*task)(void*), const char*, unsigned stack, void* context, unsigned, TaskHandle_t* handle)
{
    using namespace runtime_test;
    assert(stack == 4096 && !scheduled);
    scheduled = task;
    task_context = context;
    *handle = context;
    ++task_starts;
    return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline TickType_t xTaskGetTickCount() { return runtime_test::tick; }
inline void vTaskDelay(TickType_t ticks)
{
    runtime_test::tick += ticks;
    assert(runtime_test::tick < 20000);
    if (runtime_test::on_delay) runtime_test::on_delay();
}
inline void ulTaskNotifyTake(int, TickType_t wait) { vTaskDelay(wait); }
