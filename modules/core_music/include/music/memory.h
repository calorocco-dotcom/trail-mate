#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif
namespace music
{
inline void* allocate(std::size_t bytes)
{
#if defined(ESP_PLATFORM)
    return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::calloc(1, bytes);
#endif
}
inline void release(void* value)
{
#if defined(ESP_PLATFORM)
    heap_caps_free(value);
#else
    std::free(value);
#endif
}
template <class T, class... Args>
T* make(Args&&... args)
{
    void* memory = allocate(sizeof(T));
    return memory ? new (memory) T(static_cast<Args&&>(args)...) : nullptr;
}
template <class T>
void destroy(T*& value)
{
    if (!value) return;
    value->~T();
    release(value);
    value = nullptr;
}
} // namespace music
