#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void*;
inline int xTaskCreate(void (*task)(void*), const char*, unsigned stack, void* context, unsigned task_priority, TaskHandle_t* handle)
{
    using namespace runtime_test;
    assert(stack == 4096 && !scheduled);
    scheduled = task;
    task_context = context;
    priority = task_priority;
    pinned_core = -1;
    *handle = context;
    ++task_starts;
    return pdPASS;
}
inline int xTaskCreatePinnedToCore(void (*task)(void*), const char* name, unsigned stack, void* context, unsigned priority, TaskHandle_t* handle, int core)
{
    const auto result = xTaskCreate(task, name, stack, context, priority, handle);
    runtime_test::pinned_core = core;
    return result;
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline TickType_t xTaskGetTickCount() { return runtime_test::tick; }
inline UBaseType_t uxTaskPriorityGet(TaskHandle_t) { return runtime_test::priority; }
inline void vTaskPrioritySet(TaskHandle_t, UBaseType_t priority) { runtime_test::priority = priority; }
inline void vTaskDelay(TickType_t ticks)
{
    ++runtime_test::delays;
    runtime_test::tick += ticks;
    assert(runtime_test::tick < 20000);
    if (runtime_test::on_delay) runtime_test::on_delay();
}
inline void taskYIELD()
{
    ++runtime_test::yields;
    if (runtime_test::on_delay) runtime_test::on_delay();
}
inline void ulTaskNotifyTake(int, TickType_t wait) { vTaskDelay(wait); }
