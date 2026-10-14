#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
namespace restore_fake
{
inline bool allocation_fails = false;
inline size_t allocation_size = 0;
inline unsigned allocations = 0, frees = 0;
} // namespace restore_fake
inline void* heap_caps_malloc(size_t size, unsigned flags)
{
    assert(flags == 3);
    if (restore_fake::allocation_fails) return nullptr;
    restore_fake::allocation_size = size;
    ++restore_fake::allocations;
    return std::malloc(size);
}
inline void heap_caps_free(void* ptr)
{
    for (size_t i = 0; i < restore_fake::allocation_size; ++i) assert(static_cast<uint8_t*>(ptr)[i] == 0);
    ++restore_fake::frees;
    std::free(ptr);
}
