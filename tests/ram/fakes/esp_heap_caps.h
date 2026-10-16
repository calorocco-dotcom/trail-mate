#pragma once
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4;
inline bool allocation_fails = false;
inline bool psram_available = true;
inline size_t live_bytes = 0, allocations = 0;
inline size_t heap_caps_get_total_size(unsigned) { return psram_available ? 8 * 1024 * 1024 : 0; }
inline void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    assert(caps == ((psram_available ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) | MALLOC_CAP_8BIT));
    ++allocations;
    if (allocation_fails) return nullptr;
    auto* block = static_cast<size_t*>(std::malloc(bytes + sizeof(size_t)));
    assert(block);
    *block = bytes;
    live_bytes += bytes;
    return block + 1;
}
inline void* heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    void* memory = heap_caps_malloc(count * size, caps);
    if (memory) std::memset(memory, 0, count * size);
    return memory;
}
inline void heap_caps_free(void* memory)
{
    if (!memory) return;
    auto* block = static_cast<size_t*>(memory) - 1;
    assert(live_bytes >= *block);
    live_bytes -= *block;
    std::free(block);
}
