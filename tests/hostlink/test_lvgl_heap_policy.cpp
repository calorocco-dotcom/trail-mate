#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_INTERNAL = 2, MALLOC_CAP_8BIT = 4;
bool psram_available = true, psram_failure = false, strict_ui = false, font_scope = false;
uint32_t attempted[2]{};
unsigned attempts = 0, frees = 0;
int external_token, internal_token;
size_t heap_caps_get_total_size(uint32_t) { return psram_available ? 8 * 1024 * 1024 : 0; }
bool external_font_load_fs_scope_active() { return font_scope; }
namespace platform::ui
{
bool psramUiRequired() { return strict_ui; }
} // namespace platform::ui
void* heap_caps_malloc(size_t, uint32_t caps)
{
    assert(attempts < 2);
    attempted[attempts++] = caps;
    if (caps & MALLOC_CAP_SPIRAM) return psram_available && !psram_failure ? &external_token : nullptr;
    return &internal_token;
}
void* heap_caps_malloc_prefer(size_t size, size_t count, ...)
{
    va_list args;
    va_start(args, count);
    void* result = nullptr;
    for (size_t i = 0; i < count && !result; ++i) result = heap_caps_malloc(size, va_arg(args, uint32_t));
    va_end(args);
    return result;
}
bool esp_ptr_external_ram(void* p) { return p == &external_token; }
void* heap_caps_realloc(void*, size_t size, uint32_t caps) { return heap_caps_malloc(size, caps); }
void* heap_caps_realloc_prefer(void*, size_t size, size_t count, ...)
{
    va_list args;
    va_start(args, count);
    void* result = nullptr;
    for (size_t i = 0; i < count && !result; ++i) result = heap_caps_malloc(size, va_arg(args, uint32_t));
    va_end(args);
    return result;
}
void heap_caps_free(void*) { ++frees; }
#include "lvgl_heap_actual.inc"
int main()
{
    for (size_t size : {size_t(1), size_t(32), size_t(256), size_t(2047), size_t(2048), size_t(65536)})
    {
        attempts = 0;
        auto* result = lv_malloc_core(size);
#ifdef ARDUINO_WIO_TRACKER_L2
        assert(result == &external_token && attempted[0] == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
        assert(result == (size >= 2048 ? &external_token : &internal_token));
#endif
        assert(attempts == 1);
    }
    attempts = 0;
    assert(!lv_malloc_core(0) && !attempts);
    psram_failure = true;
    attempts = 0;
    assert(lv_malloc_core(2048) == &internal_token && attempts == 2);
    strict_ui = true;
    attempts = 0;
    assert(!lv_malloc_core(32) && attempts == 1 && attempted[0] == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    strict_ui = false;
    font_scope = true;
    attempts = 0;
    assert(!lv_malloc_core(32) && attempts == 1);
    font_scope = false;
    attempts = 0;
    assert(!lv_realloc_core(&external_token, 32) && attempts == 1 && attempted[0] == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    psram_failure = false;
    psram_available = false;
    attempts = 0;
    assert(lv_malloc_core(32) == &internal_token && attempted[0] == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    attempts = 0;
    assert(!lv_realloc_core(&internal_token, 0) && !attempts && frees == 1);
    lv_free_core(&internal_token);
    assert(frees == 2);
}
