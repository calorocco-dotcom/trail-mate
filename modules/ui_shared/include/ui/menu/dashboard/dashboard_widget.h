#pragma once
#include "lvgl.h"
#include "platform/ui/psram_ui_lease.h"
#include <cstdlib>
#include <new>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace ui::menu::dashboard
{
enum class WidgetKind : uint8_t
{
    Mesh,
    Gps,
    Recent,
    Compass,
    Agenda,
    Count
};
struct WidgetHandle
{
    lv_obj_t* root = nullptr;
    void* state = nullptr;
    void (*refresh)(void*) = nullptr;
    uint32_t period_ms = 1000;
    uint32_t next_refresh_ms = 0;
};
inline void* widget_allocate(std::size_t bytes)
{
#if defined(ESP_PLATFORM)
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
}
inline void widget_free(void* memory)
{
#if defined(ESP_PLATFORM)
    heap_caps_free(memory);
#else
    std::free(memory);
#endif
}
struct WidgetLifetime
{
    platform::ui::PsramUiLease memory;
};
template <typename T>
T* create_widget_state()
{
    void* memory = widget_allocate(sizeof(T));
    return memory ? new (memory) T{} : nullptr;
}
template <typename T>
void delete_widget_state(T* state)
{
    if (!state) return;
    state->~T();
    widget_free(state);
}
template <typename T>
WidgetHandle own_widget(lv_obj_t* root, T* state, void (*refresh)(void*), uint32_t period_ms = 1000)
{
    lv_obj_set_user_data(root, state);
    lv_obj_add_event_cb(
        root, [](lv_event_t* event)
        {
            // Child controls can borrow arrays in this state (compass points).
            // They must finish DELETE before the backing PSRAM is released.
            lv_obj_clean(lv_event_get_target_obj(event));
            delete_widget_state(static_cast<T*>(lv_event_get_user_data(event))); },
        LV_EVENT_DELETE, state);
    return {root, state, refresh, period_ms, 0};
}
} // namespace ui::menu::dashboard
