// Inspect ownership and geometry through the host/widget boundary.
#include "ui/app_runtime.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include <cmath>
#include <cstdio>
#include <emscripten.h>

namespace
{
int inspect_lines(lv_obj_t* object)
{
    if (lv_obj_check_type(object, &lv_line_class))
    {
        const auto* points = lv_line_get_points(object);
        auto* parent = lv_obj_get_parent(object);
        const auto width = lv_obj_get_content_width(parent);
        const auto height = lv_obj_get_content_height(parent);
        for (uint32_t index = 0; index < lv_line_get_point_count(object); ++index)
            if (!std::isfinite(static_cast<float>(points[index].x)) || !std::isfinite(static_cast<float>(points[index].y)) ||
                points[index].x < 0 || points[index].y < 0 || points[index].x > width || points[index].y > height) return 7;
    }
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index)
        if (const auto error = inspect_lines(lv_obj_get_child(object, index))) return error;
    return 0;
}
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE const char* native_dashboard_rects()
{
    static char text[256];
    auto* state = ui::menu::dashboard::dashboard_state();
    if (!state || !state->panel) return "{}";
    lv_area_t panel{}, parent{};
    auto* owner = lv_obj_get_parent(state->panel);
    lv_obj_get_coords(state->panel, &panel);
    lv_obj_get_coords(owner, &parent);
    std::snprintf(text, sizeof(text),
                  "{\"panel\":[%d,%d,%d,%d],\"parent\":[%d,%d,%d,%d],\"scroll\":[%d,%d],\"offset\":%ld,\"settling\":%d}",
                  panel.x1, panel.y1, panel.x2, panel.y2, parent.x1, parent.y1, parent.x2, parent.y2,
                  lv_obj_get_scroll_x(owner), lv_obj_get_scroll_y(owner), static_cast<long>(state->slide_offset), state->settling);
    return text;
}
extern "C" EMSCRIPTEN_KEEPALIVE int native_dashboard_geometry()
{
    auto* state = ui::menu::dashboard::dashboard_state();
    if (!state || !state->panel || !state->timer) return 1;
    lv_area_t panel{}, parent{};
    lv_obj_get_coords(state->panel, &panel);
    lv_obj_get_coords(lv_obj_get_parent(state->panel), &parent);
    if (panel.x1 < parent.x1 || panel.y1 < parent.y1 || panel.x2 > parent.x2 || panel.y2 > parent.y2 - 24) return 6;
    for (const auto& widget : state->widgets)
    {
        if (!widget.root) continue;
        lv_area_t card{}, grid{};
        lv_obj_get_coords(widget.root, &card);
        lv_obj_get_coords(state->grid, &grid);
        if (card.x1 < grid.x1 || card.y1 < grid.y1 || card.x2 > grid.x2 || card.y2 > grid.y2) return 2;
        if (const auto error = inspect_lines(widget.root)) return error;
    }
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t native_dashboard_tick()
{
    auto* state = ui::menu::dashboard::dashboard_state();
    return state ? state->tick : 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE int native_widget_page()
{
    auto* state = ui::menu::dashboard::dashboard_state();
    return state && (state->dock_right || state->feed_open) ? state->page : -1;
}
extern "C" EMSCRIPTEN_KEEPALIVE int native_widget_leases()
{
    return platform::ui::psram_ui_leases.load();
}
extern "C" EMSCRIPTEN_KEEPALIVE int native_widget_count()
{
    auto* state = ui::menu::dashboard::dashboard_state();
    int count = 0;
    if (state)
        for (const auto& widget : state->widgets)
            if (widget.root) ++count;
    return count;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t native_menu_focus()
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_group_get_focused(menu_g)));
}
extern "C" EMSCRIPTEN_KEEPALIVE int native_dashboard_lifecycle(int cycles)
{
    using namespace ui::menu::dashboard;
    auto* parent = ui::menu_layout::menuPanel();
    auto* grid = dashboard_state()->menu_grid;
    const auto expected_children = lv_obj_get_child_count(parent);
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        lv_obj_delete(dashboard_state()->panel);
        if (dashboard_state()) return 4;
        if (native_widget_leases() != 0) return 9;
        lv_tick_inc(1100);
        lv_timer_handler();
        init(parent, grid, {});
        init(parent, grid, {});
        lv_obj_update_layout(parent);
        if (lv_obj_get_child_count(parent) != expected_children) return 8;
        if (native_dashboard_geometry() != 0) return 5;
        lv_tick_inc(1100);
        lv_timer_handler();
    }
    return 0;
}
