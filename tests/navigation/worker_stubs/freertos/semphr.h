#pragma once
#include "freertos/FreeRTOS.h"
using SemaphoreHandle_t = bool*;
inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
    static bool locked = false;
    return host_worker::fail_mutex_create ? nullptr : &locked;
}
inline int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    if (host_worker::deny_lock || (timeout == 10 && host_worker::deny_cancel_lock) || *mutex) return 0;
    *mutex = true;
    return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(*mutex);
    *mutex = false;
}
