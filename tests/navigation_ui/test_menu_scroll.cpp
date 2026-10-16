#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/dashboard/dashboard_widgets.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_profile.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_theme.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

lv_obj_t* main_screen = nullptr;
lv_group_t* menu_g = nullptr;
lv_group_t* app_g = nullptr;
bool ui_is_overlay_active() { return false; }
namespace platform::ui::orientation
{
bool heading_supported() { return false; }
} // namespace platform::ui::orientation
namespace ui::menu_runtime
{
Scene currentScene() { return Scene::Menu; }
} // namespace ui::menu_runtime
namespace ui::i18n
{
void set_label_text(lv_obj_t* object, const char* text) { lv_label_set_text(object, text); }
} // namespace ui::i18n
namespace platform::ui
{
music::IService* music_service() { return nullptr; }
::ui::controls::IQuickControls* quick_controls_source() { return nullptr; }
} // namespace platform::ui
namespace ui::menu::dashboard
{
// Hardware-backed widget models are irrelevant to gesture ownership. Preserve
// real LVGL object sizes/flags; the real dashboard creates and destroys them.
WidgetHandle card(lv_obj_t* parent, lv_coord_t width, lv_coord_t height)
{
    auto* object = lv_obj_create(parent);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return {object};
}
WidgetHandle create_mesh_widget(lv_obj_t* p, lv_coord_t w, lv_coord_t h) { return card(p, w, h); }
WidgetHandle create_gps_widget(lv_obj_t* p, lv_coord_t w, lv_coord_t h) { return card(p, w, h); }
WidgetHandle create_recent_widget(lv_obj_t* p, lv_coord_t w, lv_coord_t h) { return card(p, w, h); }
WidgetHandle create_compass_widget(lv_obj_t* p, lv_coord_t w, lv_coord_t h) { return card(p, w, h); }
WidgetHandle create_agenda_widget(lv_obj_t* p, lv_coord_t w, lv_coord_t h, ui::agenda::IAgendaSource*) { return card(p, w, h); }
WidgetHandle create_controls_widget(lv_obj_t* p, lv_coord_t w, lv_coord_t h, ui::controls::IQuickControls*) { return card(p, w, h); }
WidgetHandle create_music_widget(lv_obj_t* p, lv_coord_t w, lv_coord_t h, music::IService*) { return card(p, w, h); }
} // namespace ui::menu::dashboard
namespace ui::menu_layout
{
lv_obj_t* s_menu_panel = nullptr;
lv_obj_t* s_app_panel = nullptr;
lv_obj_t* s_grid_panel = nullptr;
// Actual production tile creation and launcher geometry, stopping before app
// catalog/service access. Do not duplicate viewport arithmetic in this test.
#include "menu_geometry_actual.inc"
lv_obj_t* menuPanel()
{
    return s_menu_panel;
}
} // namespace ui::menu_layout
static lv_point_t pointer;
static bool down;
static void advance(unsigned milliseconds)
{
    for (unsigned i = 0; i < milliseconds; i += 10)
    {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}
int main(int argc, char** argv)
{
    lv_init();
    const int width = argc > 1 ? std::atoi(argv[1]) : 320;
    const int height = argc > 2 ? std::atoi(argv[2]) : 240;
    auto* display = lv_display_create(width, height);
    std::vector<uint16_t> pixels(width * height);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* value, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(value); });
    menu_g = lv_group_create();
    lv_group_set_default(menu_g);
    auto* input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, [](lv_indev_t*, lv_indev_data_t* data)
                         { data->point=pointer; data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED; });
    ui::menu_layout::createPanels();
    ui::menu_layout::createAppGrid();
    const auto& profile = ui::menu_profile::current();
    auto* grid = ui::menu_layout::s_grid_panel;
    for (unsigned i = 0; i < 18; ++i)
    {
        auto* button = lv_button_create(grid);
        lv_obj_set_size(button, 72, profile.card_height);
        lv_obj_set_style_radius(button, 0, 0);
    }
    lv_obj_update_layout(grid);
    ui::menu::dashboard::init(ui::menu_layout::menuPanel(), grid, {});
    const auto point = [input](int x, int y, bool pressed, unsigned wait = 40)
    { pointer={x,y}; down=pressed; lv_indev_read(input); advance(wait); };
    const auto check = [width]()
    {
        auto* state = ui::menu::dashboard::dashboard_state();
        auto* parent = ui::menu_layout::menuPanel();
        lv_obj_update_layout(parent);
        lv_area_t area;
        lv_obj_get_coords(state->panel, &area);
        std::printf("panel=[%ld,%ld,%ld,%ld] offset=%ld scroll=(%ld,%ld) root=(%ld,%ld) slide=%u settling=%u\n", long(area.x1), long(area.y1), long(area.x2), long(area.y2), long(state->slide_offset), long(lv_obj_get_scroll_x(parent)), long(lv_obj_get_scroll_y(parent)), long(lv_obj_get_scroll_x(lv_screen_active())), long(lv_obj_get_scroll_y(lv_screen_active())), unsigned(state->slide), unsigned(state->settling));
        assert(!state->pressed && !state->settling);
        assert(lv_obj_get_scroll_x(parent) == 0 && lv_obj_get_scroll_y(parent) == 0);
        assert(lv_obj_get_scroll_x(lv_screen_active()) == 0);
        if (state->feed_open) assert(area.x1 == 0 && area.x2 == width - 1);
        else assert(area.x1 == -width && area.x2 == -1);
    };
    for (unsigned cycle = 0; cycle < 12; ++cycle)
    {
        point(25, 80, true);
        for (int x = 40; x < 300; x += 15) point(x, 80 + (x % 3), true, 20);
        if (cycle == 0)
        {
            // Native focus/scroll work must not outlive a custom slide. Model
            // a pre-existing parent scroll animation, then release the finger.
            lv_obj_scroll_by(ui::menu_layout::menuPanel(), 48, 0, LV_ANIM_ON);
            advance(80);
        }
        point(300, 81, false, 400);
        check();
        point(290, 80, true);
        for (int x = 275; x > 10; x -= 15) point(x, 80 + (x % 3), true, 20);
        if (cycle == 1)
        {
            lv_obj_scroll_by(ui::menu_layout::menuPanel(), -48, 0, LV_ANIM_ON);
            advance(80);
        }
        point(10, 81, false, 400);
        check();
    }
    // A reversed, subthreshold reveal must snap back, preserving launcher scroll.
    point(25, 80, true);
    point(85, 80, true);
    point(34, 80, true);
    point(34, 80, false, 400);
    check();
    assert(!ui::menu::dashboard::isFeedOpen());
    point(width / 2, height - 60, true);
    point(width / 2, 50, true);
    point(width / 2, 50, false, 500);
    assert(lv_obj_get_scroll_y(grid) > 0);
    assert(lv_obj_get_scroll_x(ui::menu_layout::menuPanel()) == 0 && lv_obj_get_scroll_y(ui::menu_layout::menuPanel()) == 0);
    lv_area_t viewport;
    lv_obj_get_coords(grid, &viewport);
    std::printf("launcher viewport y=%ld..%ld, bars=%ld\n", long(viewport.y1), long(viewport.y2), long(profile.top_bar_height));
    assert(viewport.y1 == profile.top_bar_height && viewport.y2 == height - 1 - profile.top_bar_height);
    lv_indev_delete(input);
    lv_obj_delete(main_screen);
    lv_group_delete(menu_g);
    lv_display_delete(display);
    assert(!ui::menu::dashboard::dashboard_state());
}
