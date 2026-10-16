#include <cstddef>
extern "C" void lv_mem_init();
extern "C" void* lv_malloc_core(std::size_t);
extern "C" void lv_free_core(void*);
bool use_allocator()
{
    lv_mem_init();
    auto* memory = lv_malloc_core(16);
    lv_free_core(memory);
    return memory != nullptr;
}
