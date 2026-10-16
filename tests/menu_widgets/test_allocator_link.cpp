#include "esp_heap_caps.h"
#include <cassert>
#include <cstdlib>
bool use_allocator();
unsigned allocations = 0;
void* heap_caps_malloc(std::size_t bytes, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++allocations;
    return std::malloc(bytes);
}
void* heap_caps_realloc(void* memory, std::size_t bytes, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return std::realloc(memory, bytes);
}
void heap_caps_free(void* memory) { std::free(memory); }
int main()
{
    assert(use_allocator());
    assert(allocations == 1);
}
