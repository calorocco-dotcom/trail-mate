#include "lvgl.h"
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
#include "esp_heap_caps.h"
#include <cstdlib>
#include <cstring>

// P4's UI uses external RAM, including small label and style allocations.
// DMA and peripheral allocations retain their SDK-owned memory policies.
extern "C" void lv_mem_init(void) {}
extern "C" void lv_mem_deinit(void) {}
extern "C" lv_mem_pool_t lv_mem_add_pool(void*, size_t) { return nullptr; }
extern "C" void lv_mem_remove_pool(lv_mem_pool_t) {}
extern "C" void* lv_malloc_core(size_t bytes)
{
    if (bytes == 0) return nullptr;
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
extern "C" void* lv_realloc_core(void* memory, size_t bytes)
{
    if (bytes == 0)
    {
        heap_caps_free(memory);
        return nullptr;
    }
    return heap_caps_realloc(memory, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
extern "C" void lv_free_core(void* memory) { heap_caps_free(memory); }
extern "C" void lv_mem_monitor_core(lv_mem_monitor_t* monitor)
{
    if (monitor) std::memset(monitor, 0, sizeof(*monitor));
}
extern "C" lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
#endif
