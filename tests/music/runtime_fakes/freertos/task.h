#pragma once
#include "FreeRTOS.h"
#include "scheduler.h"
using TaskHandle_t = void*;
inline int xTaskCreate(void (*task)(void*), const char* name, unsigned stack, void* context, unsigned task_priority, TaskHandle_t* handle)
{
    using namespace runtime_test;
    if (std::strcmp(name, "music_pcm") == 0)
    {
        assert(stack == 4096 && task_priority == 5);
        consumer_core = -1;
        if (!create_consumer(task, context)) return 0;
        *handle = consumer_context;
        return pdPASS;
    }
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
    if (std::strcmp(name, "music_pcm") == 0) runtime_test::consumer_core = core;
    else runtime_test::pinned_core = core;
    return result;
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskDelete(TaskHandle_t task)
{
    if (task) runtime_test::delete_consumer();
}
inline void vTaskSuspend(TaskHandle_t)
{
    runtime_test::consumer_finished = true;
    runtime_test::consumer_yield();
    assert(false);
}
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void*>(1); }
inline TickType_t xTaskGetTickCount() { return runtime_test::tick / kTestTickMs; }
inline UBaseType_t uxTaskPriorityGet(TaskHandle_t) { return runtime_test::priority; }
inline void vTaskPrioritySet(TaskHandle_t, UBaseType_t priority) { runtime_test::priority = priority; }
inline void vTaskDelay(TickType_t ticks)
{
    ++runtime_test::delays;
    runtime_test::advance(ticks * kTestTickMs);
    assert(runtime_test::tick < 20000);
    if (runtime_test::on_delay) runtime_test::on_delay();
}
inline void taskYIELD()
{
    ++runtime_test::yields;
    runtime_test::consumer_step();
    if (runtime_test::on_delay) runtime_test::on_delay();
}
inline void ulTaskNotifyTake(int, TickType_t wait)
{
    if (runtime_test::in_consumer)
    {
        assert(wait > 0); // A high-priority empty consumer must block, not spin.
        runtime_test::consumer_yield(true);
    }
    else vTaskDelay(wait);
}
