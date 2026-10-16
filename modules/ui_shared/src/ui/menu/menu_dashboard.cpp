#include "ui/menu/menu_dashboard.h"
#include "platform/ui/music_runtime.h"
#include "platform/ui/quick_controls_runtime.h"
#include "ui/menu/menu_layout.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2) || defined(ARDUINO_T_LORA_PAGER)
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/dashboard/dashboard_widgets.h"
#include "ui/menu/menu_profile.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_theme.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::menu::dashboard
{
namespace
{
constexpr uint32_t kDashboardTimerMs = 1000;
constexpr lv_coord_t kPagerHeight = 24;
ui::agenda::IAgendaSource* s_agenda_source = nullptr;
ui::controls::IQuickControls* s_controls_source = nullptr;
::music::IService* s_music_source = nullptr;

bool dashboard_enabled()
{
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2) || defined(ARDUINO_T_LORA_PAGER)
    return true;
#else
    return std::strcmp(ui::menu_profile::current().name, "t_display_p4") == 0;
#endif
}
void refresh_dashboard(lv_timer_t*)
{
    auto* state = dashboard_state();
    if (!state || !state->active || !lv_obj_is_visible(state->panel)) return;
    ++state->tick;
    const uint32_t now = lv_tick_get();
    for (auto& widget : state->widgets)
        if (widget.root && lv_obj_is_valid(widget.root) &&
            lv_obj_get_user_data(widget.root) == widget.state && widget.refresh)
        {
            if (widget.next_refresh_ms != 0 && static_cast<int32_t>(now - widget.next_refresh_ms) < 0) continue;
            widget.next_refresh_ms = now + widget.period_ms;
            widget.refresh(widget.state);
        }
}
void clear_widgets()
{
    auto* state = dashboard_state();
    if (!state) return;
    state->widgets = {};
    lv_obj_clean(state->grid); // Root DELETE callbacks release private PSRAM.
}
WidgetHandle create_widget(WidgetKind kind, lv_obj_t* parent, lv_coord_t width, lv_coord_t height)
{
    switch (kind)
    {
    case WidgetKind::Mesh:
        return create_mesh_widget(parent, width, height);
    case WidgetKind::Gps:
        return create_gps_widget(parent, width, height);
    case WidgetKind::Recent:
        return create_recent_widget(parent, width, height);
    case WidgetKind::Compass:
        return create_compass_widget(parent, width, height);
    case WidgetKind::Agenda:
        return create_agenda_widget(parent, width, height, s_agenda_source);
    case WidgetKind::Music:
        return create_music_widget(parent, width, height, s_music_source ? s_music_source : platform::ui::music_service());
    case WidgetKind::Controls:
        return create_controls_widget(parent, width, height, s_controls_source ? s_controls_source : platform::ui::quick_controls_source());
    default:
        return {};
    }
}
void build_page()
{
    auto* state = dashboard_state();
    if (!state) return;
    clear_widgets();
    lv_obj_update_layout(state->grid);
    const auto width = lv_obj_get_content_width(state->grid);
    const auto height = lv_obj_get_content_height(state->grid);
    if (state->dock_right && state->page == 0)
    {
        const lv_coord_t card_w = (width - 12) / 2;
        const lv_coord_t card_h = (height - 12) / 2;
        for (unsigned index = 0; index < state->widgets.size(); ++index)
            state->widgets[index] = create_widget(static_cast<WidgetKind>(index), state->grid, card_w, card_h);
    }
    else
    {
        const auto kind = static_cast<WidgetKind>(state->dock_right ? state->page + 3 : state->page);
        state->widgets[0] = create_widget(kind, state->grid, width, height);
    }
    char page[24];
    std::snprintf(page, sizeof(page), "%u / %u", state->page + 1U,
                  state->dock_right ? static_cast<unsigned>(WidgetKind::Count) - 3U : static_cast<unsigned>(WidgetKind::Count));
    lv_label_set_text(state->pager, page);
    refresh_dashboard(nullptr);
}
bool change_page(int delta)
{
    auto* state = dashboard_state();
    if (!state) return false;
    const int last = static_cast<int>(WidgetKind::Count) - (state->dock_right ? 4 : 1);
    const int next = std::clamp(static_cast<int>(state->page) + delta, 0, last);
    if (next == state->page) return false;
    state->page = static_cast<uint8_t>(next);
    build_page();
    return true;
}
void set_feed_open(bool open)
{
    auto* state = dashboard_state();
    if (!state || state->dock_right || state->feed_open == open) return;
    state->feed_open = open;
    if (open)
    {
        lv_obj_remove_flag(state->panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(state->panel);
        build_page();
        if (state->timer) lv_timer_resume(state->timer);
    }
    else
    {
        clear_widgets();
        lv_obj_add_flag(state->panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->menu_grid, LV_OBJ_FLAG_HIDDEN);
        if (state->timer) lv_timer_pause(state->timer);
    }
}
void pager_button(lv_event_t* event)
{
    const auto delta = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (delta == 0) set_feed_open(false);
    else change_page(static_cast<int>(delta));
}
void add_pager_button(lv_obj_t* parent, const char* text, lv_align_t align, lv_coord_t x, intptr_t delta)
{
    auto* button = lv_button_create(parent);
    lv_group_remove_obj(button); // Encoder focus on the menu belongs to app icons.
    lv_obj_set_size(button, delta == 0 ? 68 : 38, kPagerHeight - 2);
    lv_obj_align(button, align, x, 0);
    lv_obj_set_style_bg_color(button, ui::theme::surface_alt(), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    auto* label = lv_label_create(button);
    ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_color(label, ui::theme::text(), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, pager_button, LV_EVENT_CLICKED, reinterpret_cast<void*>(delta));
}
bool belongs_to(lv_obj_t* object, lv_obj_t* parent)
{
    for (auto* current = object; current; current = lv_obj_get_parent(current))
        if (current == parent) return true;
    return false;
}
void menu_pointer(lv_event_t* event)
{
    auto* state = dashboard_state();
    auto* input = static_cast<lv_indev_t*>(lv_event_get_current_target(event));
    if (!state || !state->active || !input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) return;
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
    if (ui::menu_runtime::currentScene() != ui::menu_runtime::Scene::Menu ||
        ui_is_overlay_active() || !lv_obj_is_visible(ui::menu_layout::menuPanel()) ||
        (lv_group_get_default() && lv_group_get_default() != menu_g))
    {
        state->pressed = false;
        return;
    }
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED)
    {
        state->pressed = false;
        // A slider owns its complete drag, even when a page threshold is crossed.
        for (auto* object = lv_indev_get_active_obj(); object; object = lv_obj_get_parent(object))
            if (lv_obj_check_type(object, &lv_slider_class)) return;
        lv_area_t area;
        lv_obj_get_coords(ui::menu_layout::menuPanel(), &area);
        if (point.x < area.x1 || point.x > area.x2 || point.y < area.y1 || point.y > area.y2) return;
        lv_obj_get_coords(state->panel, &area);
        state->inside_dashboard = point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2;
        state->pointer = input;
        state->start = point;
        state->pressed = true;
        return;
    }
    if (!state->pressed || state->pointer != input) return;
    auto* target = lv_indev_get_active_obj();
    if (target && !belongs_to(target, ui::menu_layout::menuPanel()))
    {
        state->pressed = false; // A modal or another screen owns this press.
        return;
    }
    const int dx = point.x - state->start.x;
    const int dy = point.y - state->start.y;
    const int threshold = std::clamp<lv_coord_t>(lv_obj_get_width(state->panel) / 7, 36, 96);
    const bool horizontal = std::abs(dx) >= threshold && std::abs(dx) > std::abs(dy) + 16;
    const bool vertical = std::abs(dy) >= 36 && std::abs(dy) > std::abs(dx) + 16;
    int action = 0;
    if (state->dock_right && horizontal) action = state->inside_dashboard ? (dx < 0 ? 2 : -2) : 3;
    else if (!state->dock_right && horizontal && dx > 0 && !state->feed_open) action = 1;
    else if (!state->dock_right && horizontal && dx < 0 && state->feed_open) action = -1;
    else if (!state->dock_right && state->feed_open && vertical) action = dy < 0 ? 2 : -2;
    if (action)
    {
        state->pressed = false;
        // Cancel the original object's click before hiding/deleting its tree.
        // A single physical press may perform exactly one navigation action.
        lv_indev_reset(input, nullptr);
        lv_indev_wait_release(input);
        lv_indev_stop_processing(input);
        if (std::abs(action) == 1) set_feed_open(action > 0);
        else if (std::abs(action) == 2) change_page(action > 0 ? 1 : -1);
        return;
    }
    if (code == LV_EVENT_RELEASED) state->pressed = false;
}
void panel_deleted(lv_event_t* event)
{
    auto* state = dashboard_state();
    if (!state || lv_event_get_target(event) != state->panel) return;
    if (state->feed_open && state->menu_grid && lv_obj_is_valid(state->menu_grid))
        lv_obj_remove_flag(state->menu_grid, LV_OBJ_FLAG_HIDDEN);
    dashboard_reset();
}
} // namespace

void bindAgendaSource(ui::agenda::IAgendaSource* source) { s_agenda_source = source; }
void bindControlsSource(ui::controls::IQuickControls* source) { s_controls_source = source; }
void bindMusicSource(::music::IService* source) { s_music_source = source; }

bool isFeedOpen()
{
    auto* state = dashboard_state();
    return state && state->active && state->feed_open;
}
bool handleEncoder(int delta)
{
    if (!isFeedOpen() || delta == 0 || ui_is_overlay_active() ||
        ui::menu_runtime::currentScene() != ui::menu_runtime::Scene::Menu ||
        (lv_group_get_default() && lv_group_get_default() != menu_g)) return false;
    change_page(delta > 0 ? 1 : -1);
    return true;
}
bool handleKey(uint32_t key, bool at_left_edge)
{
    auto* state = dashboard_state();
    if (!state || !state->active || state->dock_right ||
        ui::menu_runtime::currentScene() != ui::menu_runtime::Scene::Menu || ui_is_overlay_active() ||
        (lv_group_get_default() && lv_group_get_default() != menu_g)) return false;
    if (state->feed_open)
    {
        for (auto& widget : state->widgets)
            if (widget.key && widget.key(widget.state, key)) return true;
        if (key == 'w' || key == 'W' || key == LV_KEY_UP || key == LV_KEY_PREV) change_page(-1);
        else if (key == 's' || key == 'S' || key == LV_KEY_DOWN || key == LV_KEY_NEXT) change_page(1);
        else if (key == 'd' || key == 'D' || key == LV_KEY_RIGHT || key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) set_feed_open(false);
        else if (key != LV_KEY_ENTER && key != 'a' && key != 'A' && key != LV_KEY_LEFT) return false;
        return true;
    }
    if (at_left_edge && (key == LV_KEY_LEFT || key == 'a' || key == 'A'))
    {
        set_feed_open(true);
        return true;
    }
    return false;
}

void init(lv_obj_t* menu_panel, lv_obj_t* grid_panel, const ui::menu_layout::InitOptions&)
{
    if (!dashboard_enabled() || !menu_panel || !grid_panel) return;
    if (auto* previous = dashboard_state())
    {
        auto* panel = previous->panel;
        if (previous->feed_open && lv_obj_is_valid(previous->menu_grid))
            lv_obj_remove_flag(previous->menu_grid, LV_OBJ_FLAG_HIDDEN);
        dashboard_reset();
        if (panel && lv_obj_is_valid(panel)) lv_obj_delete(panel);
    }
    if (!dashboard_create()) return; // PSRAM exhaustion never falls back to RAM.
    platform::ui::PsramUiLease allocation_scope;
    auto& state = *dashboard_state();
    state.menu_grid = grid_panel;
    lv_obj_update_layout(menu_panel);
    const auto& profile = ui::menu_profile::current();
    const auto screen_w = lv_obj_get_content_width(menu_panel);
    const auto screen_h = lv_obj_get_content_height(menu_panel);
    state.dock_right = std::strcmp(profile.name, "t_display_p4") == 0;
    const lv_coord_t width = state.dock_right ? screen_w - lv_obj_get_width(grid_panel) - 16 : screen_w;
    const lv_coord_t height = state.dock_right ? screen_h - profile.grid_top_offset - profile.top_bar_height - 8 : screen_h - profile.top_bar_height;
    state.panel = lv_obj_create(menu_panel);
    lv_obj_set_size(state.panel, width, height);
    lv_obj_align(state.panel, state.dock_right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, state.dock_right ? -8 : 0, state.dock_right ? profile.grid_top_offset : 0);
    lv_obj_set_style_bg_color(state.panel, ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(state.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state.panel, 0, 0);
    lv_obj_set_style_pad_all(state.panel, 0, 0);
    lv_obj_clear_flag(state.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(state.panel, panel_deleted, LV_EVENT_DELETE, nullptr);
    state.grid = lv_obj_create(state.panel);
    lv_obj_set_size(state.grid, width, height - kPagerHeight);
    lv_obj_set_style_bg_opa(state.grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state.grid, 0, 0);
    lv_obj_set_style_pad_all(state.grid, 0, 0);
    lv_obj_set_style_pad_row(state.grid, 12, 0);
    lv_obj_set_style_pad_column(state.grid, 12, 0);
    lv_obj_set_flex_flow(state.grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(state.grid, LV_OBJ_FLAG_SCROLLABLE);
    state.pager = lv_label_create(state.panel);
    lv_obj_align(state.pager, LV_ALIGN_BOTTOM_MID, 0, -3);
    if (!state.dock_right) add_pager_button(state.panel, "Menu", LV_ALIGN_BOTTOM_LEFT, 0, 0);
    add_pager_button(state.panel, state.dock_right ? LV_SYMBOL_LEFT : LV_SYMBOL_UP, LV_ALIGN_BOTTOM_RIGHT, -42, -1);
    add_pager_button(state.panel, state.dock_right ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN, LV_ALIGN_BOTTOM_RIGHT, 0, 1);
    state.timer = lv_timer_create(refresh_dashboard, kDashboardTimerMs, nullptr);
    if (state.dock_right) build_page();
    else
    {
        lv_obj_add_flag(state.panel, LV_OBJ_FLAG_HIDDEN);
        if (state.timer) lv_timer_pause(state.timer);
    }
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
    {
        if (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) continue;
        lv_indev_remove_event_cb_with_user_data(input, menu_pointer, nullptr);
        lv_indev_add_event_cb(input, menu_pointer, LV_EVENT_ALL, nullptr);
    }
}
void bringToFront()
{
    if (auto* state = dashboard_state()) lv_obj_move_foreground(state->panel);
}
void refresh_localized_text()
{
    if (auto* state = dashboard_state(); state && state->active && lv_obj_is_visible(state->panel)) build_page();
}
void setActive(bool active)
{
    auto* state = dashboard_state();
    if (!state || state->active == active) return;
    state->active = active;
    state->pressed = false;
    if (!active)
    {
        if (state->feed_open) set_feed_open(false);
        else clear_widgets();
        if (state->timer) lv_timer_pause(state->timer);
    }
    else if (state->dock_right)
    {
        build_page();
        if (state->timer) lv_timer_resume(state->timer);
    }
}
} // namespace ui::menu::dashboard
#endif
