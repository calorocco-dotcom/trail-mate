#pragma once
#include <cstdlib>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_INTERNAL = 2, MALLOC_CAP_8BIT = 4;
namespace image_fake
{
inline bool psram = true, allocation_fail = false;
inline unsigned allocations = 0, live = 0, last_caps = 0;
inline unsigned internal_allocations = 0, fail_at = 0;
} // namespace image_fake
inline size_t heap_caps_get_total_size(unsigned caps) { return caps == MALLOC_CAP_SPIRAM && image_fake::psram ? 8192 : 0; }
inline void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    ++image_fake::allocations;
    if (caps & MALLOC_CAP_INTERNAL) ++image_fake::internal_allocations;
    image_fake::last_caps = caps;
    if (image_fake::allocation_fail || image_fake::allocations == image_fake::fail_at) return nullptr;
    auto* p = std::malloc(bytes);
    if (p) ++image_fake::live;
    return p;
}
inline void heap_caps_free(void* p)
{
    if (p)
    {
        --image_fake::live;
        std::free(p);
    }
}
