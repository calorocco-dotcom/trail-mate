#include "ui/assets/fonts/font_utils.h"
#include "ui/components/dialog_frame.h"
#include "ui/widgets/top_bar.h"
#include <cassert>
#include <cstdio>
#include <vector>

// Keep the real LVGL layout, shared frame, TopBar and button code; replace only
// app services that are irrelevant to the component's geometry and lifetime.
namespace ui::page_profile
{
const PageLayoutProfile& current()
{
    static PageLayoutProfile profile{};
    return profile;
}
const lv_font_t* resolve_body_font() { return &lv_font_montserrat_14; }
const lv_font_t* resolve_title_font() { return &lv_font_montserrat_14; }
} // namespace ui::page_profile
namespace ui::i18n
{
const char* tr(const char* text) { return text; }
void set_label_text(lv_obj_t* label, const char* text) { lv_label_set_text(label, text); }
const lv_font_t* active_ui_font_fallback() { return nullptr; }
const lv_font_t* active_content_font_fallback() { return nullptr; }
bool ensure_content_font_for_text(const char*) { return true; }
} // namespace ui::i18n
namespace ui::fonts
{
LocalizedFontBinding* localized_font_binding_storage() { return nullptr; }
std::size_t localized_font_binding_storage_size() { return 0; }
} // namespace ui::fonts
namespace ui::widgets::top_bar_power
{
void bind(TopBar&) {}
void unbind(TopBar&) {}
} // namespace ui::widgets::top_bar_power

int main()
{
    lv_init();
    for (const int width : {240, 320, 480})
    {
        auto* display = lv_display_create(width, 240);
        std::vector<uint16_t> pixels(width * 240);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                                { lv_display_flush_ready(d); });
        auto* screen = lv_screen_active();
        ui::widgets::TopBar page_bar{};
        ui::widgets::TopBarConfig bar_config{};
        bar_config.create_back = false;
        bar_config.power_indicator = false;
        ui::widgets::top_bar_init(page_bar, screen, bar_config);
        const auto retained = lv_obj_get_child_count(screen);
        for (const bool keyboard : {false, true})
        {
            struct Context
            {
                lv_obj_t* overlay = nullptr;
                unsigned closed = 0;
            } context;
            const auto frame = ui::components::dialog_frame::create(screen,
                                                                    {"Navigation", 304, 180, [](lv_event_t* event)
                                                                     {
                                                                         auto& context = *static_cast<Context*>(lv_event_get_user_data(event));
                                                                         ++context.closed;
                                                                         lv_obj_del(context.overlay);
                                                                         context.overlay = nullptr;
                                                                     },
                                                                     nullptr, &context});
            context.overlay = frame.overlay;
            for (unsigned line = 0; line < 20; ++line)
                lv_label_set_text(lv_label_create(frame.body), "Scrollable route details");
            lv_obj_update_layout(screen);
            auto* header = lv_obj_get_child(frame.panel, 0);
            assert(lv_obj_get_height(header) == lv_obj_get_height(page_bar.container));
            assert(lv_obj_get_width(frame.panel) <= width - 12);
            assert(lv_obj_get_width(frame.close) == lv_obj_get_height(frame.close));
            assert(lv_obj_get_height(frame.close) <= lv_obj_get_height(header));
            lv_area_t panel{}, body{}, close{}, title{};
            lv_obj_get_coords(frame.panel, &panel);
            lv_obj_get_coords(frame.body, &body);
            lv_obj_get_coords(frame.close, &close);
            lv_obj_get_coords(header, &title);
            assert(close.x2 <= panel.x2 && close.y1 >= title.y1 && close.y2 <= title.y2);
            assert(body.y1 > title.y2 && body.y2 <= panel.y2);
            lv_obj_scroll_to_y(frame.body, 120, LV_ANIM_OFF);
            lv_obj_update_layout(screen);
            assert(lv_obj_get_y(header) == 0);
            if (keyboard)
            {
                uint32_t enter = LV_KEY_ENTER;
                lv_obj_send_event(frame.close, LV_EVENT_KEY, &enter);
            }
            else lv_obj_send_event(frame.close, LV_EVENT_CLICKED, nullptr);
            assert(context.closed == 1 && !context.overlay);
            assert(lv_obj_get_child_count(screen) == retained);
        }
        lv_display_delete(display);
        std::printf("%dpx: shared title-bar metrics, bounded body, fixed header and touch/key close passed.\n", width);
    }
}
