#pragma once
#include "freertos/FreeRTOS.h"
inline int xTaskCreate(void (*entry)(void*), const char*, unsigned stack, void* context, unsigned, void*)
{
    assert(stack == 4096 && !host_worker::task);
    if (host_worker::fail_task_create) return 0;
    host_worker::task = entry;
    host_worker::context = context;
    return pdPASS;
}
inline void vTaskDelay(unsigned)
{
    ++host_worker::ticks;
    if (host_worker::delay_hook) host_worker::delay_hook();
}
inline void vTaskDelete(void*) {}
