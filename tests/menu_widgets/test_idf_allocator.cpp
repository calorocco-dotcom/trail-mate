#include "esp_heap_caps.h"
#include <cassert>
#include <cstdlib>
#include <initializer_list>
bool fail_external = false;
unsigned attempts = 0;
void* heap_caps_malloc(std::size_t bytes, uint32_t caps)
{
    ++attempts;
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return fail_external ? nullptr : std::malloc(bytes);
}
void* heap_caps_realloc(void* memory, std::size_t bytes, uint32_t caps)
{
    ++attempts;
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return fail_external ? nullptr : std::realloc(memory, bytes);
}
void heap_caps_free(void* memory) { std::free(memory); }
#include "../../platform/esp/idf_common/src/lvgl_memory.cpp"
int main()
{
    lv_mem_init();
    assert(lv_malloc_core(0) == nullptr && attempts == 0);
    for (std::size_t bytes : {std::size_t(1), std::size_t(16), std::size_t(4096)})
    {
        auto* memory = lv_malloc_core(bytes);
        assert(memory);
        fail_external = true;
        const auto before = attempts;
        assert(lv_malloc_core(bytes) == nullptr && attempts == before + 1);
        assert(lv_realloc_core(memory, bytes * 2) == nullptr);
        fail_external = false;
        memory = lv_realloc_core(memory, bytes * 2);
        assert(memory);
        assert(lv_realloc_core(memory, 0) == nullptr);
    }
    lv_mem_deinit();
}
