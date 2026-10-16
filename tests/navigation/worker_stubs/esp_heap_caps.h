#pragma once
#include "host_worker.h"
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4;
inline size_t heap_caps_get_total_size(unsigned caps)
{
    assert(caps == MALLOC_CAP_SPIRAM);
    return host_worker::refuse ? 0 : 8 * 1024 * 1024;
}
inline size_t heap_caps_get_free_size(unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) || caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return caps & MALLOC_CAP_SPIRAM ? (host_worker::refuse ? 0 : 8 * 1024 * 1024 - host_worker::live) : 65536;
}
inline size_t heap_caps_get_largest_free_block(unsigned caps) { return heap_caps_get_free_size(caps); }
inline void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && alignment <= alignof(max_align_t));
    if (host_worker::refuse || host_worker::fail_allocation) return nullptr;
    auto* data = std::malloc(bytes);
    assert(data);
    host_worker::allocations[data] = bytes;
    host_worker::live += bytes;
    host_worker::peak = std::max(host_worker::live, host_worker::peak);
    return data;
}
inline void heap_caps_free(void* data)
{
    if (!data) return;
    assert(host_worker::allocations.count(data));
    host_worker::live -= host_worker::allocations.at(data);
    host_worker::allocations.erase(data);
    std::free(data);
}
