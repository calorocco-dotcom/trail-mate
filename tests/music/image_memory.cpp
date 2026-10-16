#include "lvgl.h"
#include "platform/ui/psram_ui_lease.h"
#include "src/core/lv_global.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
// Codec-only host allocator: check that all PNG allocations hold the strict PSRAM lease.
extern "C" void* lv_malloc(size_t size)
{
    assert(platform::ui::psramUiRequired());
    return std::malloc(size);
}
extern "C" void* lv_realloc(void* data, size_t size)
{
    assert(platform::ui::psramUiRequired());
    return std::realloc(data, size);
}
extern "C" void lv_free(void* data) { std::free(data); }
extern "C" void* lv_memcpy(void* out, const void* in, size_t size) { return std::memcpy(out, in, size); }
extern "C" void lv_memset(void* out, uint8_t value, size_t size) { std::memset(out, value, size); }
lv_global_t lv_global{};
extern "C" lv_draw_buf_t* lv_draw_buf_create_ex(const lv_draw_buf_handlers_t*, uint32_t width, uint32_t height, lv_color_format_t format, uint32_t stride)
{
    auto* buffer = static_cast<lv_draw_buf_t*>(lv_malloc(sizeof(lv_draw_buf_t)));
    if (!buffer) return nullptr;
    *buffer = {};
    buffer->header.w = width;
    buffer->header.h = height;
    buffer->header.cf = format;
    buffer->header.stride = stride;
    buffer->data_size = height * stride;
    buffer->data = static_cast<uint8_t*>(lv_malloc(buffer->data_size));
    if (!buffer->data)
    {
        lv_free(buffer);
        return nullptr;
    }
    return buffer;
}
extern "C" void lv_draw_buf_destroy(lv_draw_buf_t* buffer)
{
    if (buffer)
    {
        lv_free(buffer->data);
        lv_free(buffer);
    }
}
