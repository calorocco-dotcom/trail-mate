#pragma once
#include "FreeRTOS.h"
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <ucontext.h>
#endif

// Deterministic, single-threaded task contexts. A blocking fake I2S write
// yields back to the decoder; advance() can run only the consumer for a long
// UI/SD stall. The production task entry/stop/join paths remain unchanged.
namespace runtime_test
{
inline void (*consumer_entry)(void*) = nullptr;
inline void* consumer_context = nullptr;
inline bool in_consumer = false, consumer_idle = false, consumer_finished = false;
inline bool fail_consumer_task = false;
inline int consumer_core = -1;
inline unsigned consumer_starts = 0, consumer_deletes = 0;
#ifdef _WIN32
inline void *caller_fiber = nullptr, *consumer_fiber = nullptr;
inline void CALLBACK trampoline(void*)
{
    consumer_entry(consumer_context);
    assert(false);
}
#else
inline ucontext_t caller_fiber{}, consumer_fiber{};
inline char consumer_stack[128 * 1024];
inline void trampoline()
{
    consumer_entry(consumer_context);
    assert(false);
}
#endif
inline void consumer_yield(bool idle = false)
{
    assert(in_consumer);
    consumer_idle = idle;
#ifdef _WIN32
    SwitchToFiber(caller_fiber);
#else
    swapcontext(&consumer_fiber, &caller_fiber);
#endif
}
inline void consumer_step()
{
    if (!consumer_entry || consumer_finished) return;
    const auto saved = priority;
    priority = 5;
    in_consumer = true;
#ifdef _WIN32
    SwitchToFiber(consumer_fiber);
#else
    swapcontext(&caller_fiber, &consumer_fiber);
#endif
    in_consumer = false;
    priority = saved;
}
inline void advance(TickType_t duration)
{
    const auto until = tick + duration;
    do
    {
        const auto before = tick;
        consumer_step();
        if (tick == before && (consumer_idle || consumer_finished || !consumer_entry)) tick = until;
    } while (tick < until);
}
inline bool create_consumer(void (*entry)(void*), void* context)
{
    assert(!consumer_entry);
    if (fail_consumer_task) return false;
    consumer_entry = entry;
    consumer_context = context;
    consumer_finished = consumer_idle = false;
#ifdef _WIN32
    if (!caller_fiber) caller_fiber = ConvertThreadToFiber(nullptr);
    assert(caller_fiber);
    consumer_fiber = CreateFiber(128 * 1024, trampoline, nullptr);
    assert(consumer_fiber);
#else
    getcontext(&consumer_fiber);
    consumer_fiber.uc_stack.ss_sp = consumer_stack;
    consumer_fiber.uc_stack.ss_size = sizeof(consumer_stack);
    consumer_fiber.uc_link = nullptr;
    makecontext(&consumer_fiber, trampoline, 0);
#endif
    ++consumer_starts;
    return true;
}
inline void delete_consumer()
{
    assert(consumer_entry && consumer_finished && !in_consumer);
#ifdef _WIN32
    DeleteFiber(consumer_fiber);
    consumer_fiber = nullptr;
#endif
    consumer_entry = nullptr;
    ++consumer_deletes;
}
} // namespace runtime_test
