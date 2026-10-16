// Browser display/input adapter. Page objects are built by unchanged firmware sources.
#include "app/app_facade_access.h"
#include "lvgl.h"
#include "platform/ui/walkie_runtime.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/screens/calculator/calculator_page_runtime.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <emscripten.h>
#include <string>
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/board_profile.h"
#endif

lv_group_t* app_g = nullptr;
lv_group_t* menu_g = nullptr;
lv_obj_t* main_screen = nullptr;
static lv_display_t* display;
static lv_indev_t* pointer_device;
static lv_group_t* bound_input_group;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PANEL_HI8561)
static constexpr auto panel = boards::t_display_p4::kBoardProfile.hi8561_panel;
#else
static constexpr auto panel = boards::t_display_p4::kBoardProfile.rm69a10_panel;
#endif
static constexpr int preview_width = panel.height;
static constexpr int preview_height = panel.width;
#else
static constexpr int preview_width = 480;
static constexpr int preview_height = 240;
#endif
static uint16_t pixels[preview_width * preview_height];
static int pointer_x, pointer_y;
static bool pointer_down;
static ui::page_profile::PageLayoutProfile page_profile;
static std::string snapshot;
static bool initialized = false;
void preview_exit_page();
void preview_bind_app();
int preview_current_page();
extern "C" void native_open_page(int page);

// A preview has no attached hardware keyboard; keep P4's touch IME available.
lv_indev_t* lv_get_keyboard_indev() { return nullptr; }

EM_JS(void, send_frame, (uint32_t ptr, int width, int height), {
    if (Module.onFrame) Module.onFrame(ptr, width, height);
});
EM_JS(void, request_menu, (), {
    if (Module.onExit) Module.onExit();
});

void set_default_group(lv_group_t* group)
{
    // Native keys/encoder are dispatched directly, independently of touch.
    // A pointer indev cannot retain a keypad/encoder group in LVGL.
    bound_input_group = group;
    lv_group_set_default(group);
}

static lv_group_t* input_group()
{
    // Pages may clear LVGL's auto-add default after binding their input group.
    return bound_input_group ? bound_input_group : lv_group_get_default();
}
void ui_request_exit_to_menu()
{
    // Do not destroy the current page while its LVGL event is being dispatched.
    static bool pending = false;
    if (pending) return;
    pending = true;
    lv_async_call([](void*)
                  {
        pending = false;
        native_open_page(10); },
                  nullptr);
}

static void append_json_string(const char* value)
{
    snapshot += '"';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p)
    {
        if (*p == '"' || *p == '\\')
        {
            snapshot += '\\';
            snapshot += *p;
        }
        else if (*p == '\n') snapshot += "\\n";
        else if (*p >= 32) snapshot += *p;
    }
    snapshot += '"';
}

static void collect_labels(lv_obj_t* obj)
{
    if (!lv_obj_is_visible(obj)) return;
    if (lv_obj_check_type(obj, &lv_slider_class))
    {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        char data[220];
        std::snprintf(data, sizeof(data), "%s{\"slider\":true,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"value\":%d}",
                      snapshot.size() > 1 ? "," : "", static_cast<int>(area.x1), static_cast<int>(area.y1),
                      static_cast<int>(lv_area_get_width(&area)), static_cast<int>(lv_area_get_height(&area)), static_cast<int>(lv_slider_get_value(obj)));
        snapshot += data;
    }
    if (lv_obj_check_type(obj, &lv_textarea_class))
    {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        char data[200];
        std::snprintf(data, sizeof(data), "%s{\"id\":%u,\"input\":true,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"text\":", snapshot.size() > 1 ? "," : "", static_cast<unsigned>(reinterpret_cast<uintptr_t>(obj)), static_cast<int>(area.x1), static_cast<int>(area.y1), static_cast<int>(lv_area_get_width(&area)), static_cast<int>(lv_area_get_height(&area)));
        snapshot += data;
        append_json_string(lv_textarea_get_text(obj));
        snapshot += '}';
    }
    if (lv_obj_check_type(obj, &lv_label_class))
    {
        const char* text = lv_label_get_text(obj);
        lv_obj_t* parent = lv_obj_get_parent(obj);
        const bool button = parent && lv_obj_has_class(parent, &lv_button_class);
        lv_area_t area;
        lv_obj_get_coords(button ? parent : obj, &area);
        char data[200];
        std::snprintf(data, sizeof(data), "%s{\"id\":%u,\"button\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"text\":",
                      snapshot.size() > 1 ? "," : "", static_cast<unsigned>(reinterpret_cast<uintptr_t>(parent)),
                      button ? "true" : "false", static_cast<int>(area.x1), static_cast<int>(area.y1),
                      static_cast<int>(lv_area_get_width(&area)), static_cast<int>(lv_area_get_height(&area)));
        snapshot += data;
        append_json_string(text);
        snapshot += '}';
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) collect_labels(lv_obj_get_child(obj, i));
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE void native_init(int deck)
    {
        if (!initialized)
        {
            lv_init();
            setenv("TRAIL_MATE_SD_ROOT", "/sd", 1);
            setenv("TRAIL_MATE_SETTINGS_ROOT", "/settings", 1);
            preview_bind_app();
            display = lv_display_create(preview_width, preview_height);
            lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
            lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
            lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t* buffer)
                                    {
            send_frame(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer)), lv_display_get_horizontal_resolution(d), lv_display_get_vertical_resolution(d));
            lv_display_flush_ready(d); });
            pointer_device = lv_indev_create();
            lv_indev_set_type(pointer_device, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(pointer_device, [](lv_indev_t*, lv_indev_data_t* data)
                                 {
            data->point.x = pointer_x; data->point.y = pointer_y;
            data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED; });
            app_g = lv_group_create();
            set_default_group(app_g);
            ui::i18n::set_locale("en", false);
            initialized = true;
        }
        preview_exit_page();
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        lv_display_set_resolution(display, preview_width, preview_height);
        page_profile = ui::page_profile::make_t_display_p4_profile();
#else
        lv_display_set_resolution(display, deck ? 320 : 480, deck ? 240 : 222);
        page_profile = deck ? ui::page_profile::make_tdeck_profile() : ui::page_profile::make_pager_profile();
#endif
        ui::page_profile::set_active_profile(&page_profile);
        native_open_page(0);
        lv_obj_update_layout(lv_screen_active());
        lv_refr_now(display);
    }
    EMSCRIPTEN_KEEPALIVE void native_tick(int elapsed)
    {
        lv_tick_inc(elapsed);
        lv_timer_handler();
        ui::i18n::on_lvgl_frame_completed();
    }
    EMSCRIPTEN_KEEPALIVE void native_pointer(int x, int y, int down)
    {
        pointer_x = x;
        pointer_y = y;
        pointer_down = down != 0;
        lv_indev_read(pointer_device);
    }
    EMSCRIPTEN_KEEPALIVE void native_activate(uint32_t id)
    {
        auto* button = reinterpret_cast<lv_obj_t*>(static_cast<uintptr_t>(id));
        if (lv_obj_is_valid(button) && !lv_obj_has_state(button, LV_STATE_DISABLED)) lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        lv_refr_now(display);
    }
    EMSCRIPTEN_KEEPALIVE void native_key(int key)
    {
        if (preview_current_page() == 10 && ui::menu::dashboard::handleKey(key)) return;
        if (key == LV_KEY_ENTER && preview_current_page() == 10)
        {
            auto* group = input_group();
            auto* focused = group ? lv_group_get_focused(group) : nullptr;
            if (focused && !lv_obj_has_state(focused, LV_STATE_DISABLED))
            {
                lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
            }
            return;
        }
        if (key < 128 && ui::menu_runtime::handleShortcutKey(static_cast<char>(key), 1)) return;
        if (auto* group = input_group()) lv_group_send_data(group, key);
    }
    EMSCRIPTEN_KEEPALIVE void native_focus(uint32_t id)
    {
        auto* obj = reinterpret_cast<lv_obj_t*>(static_cast<uintptr_t>(id));
        if (lv_obj_is_valid(obj)) lv_group_focus_obj(obj);
    }
    EMSCRIPTEN_KEEPALIVE void native_set_text(uint32_t id, const char* value)
    {
        auto* obj = reinterpret_cast<lv_obj_t*>(static_cast<uintptr_t>(id));
        if (lv_obj_is_valid(obj) && lv_obj_check_type(obj, &lv_textarea_class)) lv_textarea_set_text(obj, value);
    }
    EMSCRIPTEN_KEEPALIVE void native_protocol(int protocol) { app::configFacade().switchMeshProtocol(static_cast<chat::MeshProtocol>(protocol), false); }
    EMSCRIPTEN_KEEPALIVE int native_protocol_value() { return static_cast<int>(app::configFacade().getMeshProtocol()); }
    EMSCRIPTEN_KEEPALIVE int native_page_value() { return preview_current_page(); }
    EMSCRIPTEN_KEEPALIVE void native_key_state(int key, int pressed)
    {
        if (key == ' ')
        {
            if (ui::menu_runtime::handleWalkieKey(' ', pressed)) return;
            if (preview_current_page() == 3)
            {
                platform::ui::walkie::set_ptt(pressed != 0);
                return;
            }
        }
        if (pressed) native_key(key);
    }
    EMSCRIPTEN_KEEPALIVE void native_encoder(int delta)
    {
        if (ui::menu::dashboard::handleEncoder(delta)) return;
        if (auto* group = input_group())
        {
            if (delta < 0) lv_group_focus_prev(group);
            else lv_group_focus_next(group);
        }
    }
    EMSCRIPTEN_KEEPALIVE const char* native_snapshot()
    {
        snapshot = "[";
        collect_labels(lv_screen_active());
        collect_labels(lv_layer_top());
        snapshot += ']';
        return snapshot.c_str();
    }
}
