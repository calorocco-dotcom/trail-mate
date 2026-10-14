#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline size_t directory_free_bytes = 65536, directory_largest_block = 65536, directory_live_allocations = 0;
inline bool directory_allocation_fail = false;
inline size_t heap_caps_get_free_size(uint32_t) { return directory_free_bytes; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return directory_largest_block; }
inline void* heap_caps_malloc(size_t bytes, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (directory_allocation_fail) return nullptr;
    void* value = std::malloc(bytes);
    if (value) ++directory_live_allocations;
    return value;
}
inline void heap_caps_free(void* value)
{
    if (value)
    {
        assert(directory_live_allocations);
        --directory_live_allocations;
        std::free(value);
    }
}
