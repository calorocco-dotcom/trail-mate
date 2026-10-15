// Exercise the real dashboard's geometry and destruction boundaries.
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include <cmath>
#include <emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE int native_dashboard_geometry()
{
    auto& state = ui::menu::dashboard::dashboard_state();
    if (!state.panel || !state.timer) return 1;
    for (auto* object : {state.mesh.orbit_core, state.gps.radar, state.compass.dial, state.compass.needle})
        if (!object || lv_obj_get_width(object) <= 0 || lv_obj_get_height(object) <= 0) return 2;
    lv_area_t panel{}, parent{};
    lv_obj_get_coords(state.panel, &panel);
    lv_obj_get_coords(lv_obj_get_parent(state.panel), &parent);
    if (panel.x1 < parent.x1 || panel.y1 < parent.y1 || panel.x2 > parent.x2 || panel.y2 >= parent.y2 - 24) return 6;
    const float diameter = lv_obj_get_content_width(state.compass.dial);
    for (const auto& point : state.compass.needle_points)
        if (!std::isfinite(static_cast<float>(point.x)) || !std::isfinite(static_cast<float>(point.y)) ||
            point.x < 0 || point.y < 0 || point.x > diameter || point.y > diameter) return 7;
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t native_dashboard_tick()
{
    return ui::menu::dashboard::dashboard_state().tick;
}

extern "C" EMSCRIPTEN_KEEPALIVE int native_dashboard_lifecycle(int cycles)
{
    using namespace ui::menu::dashboard;
    auto* parent = ui::menu_layout::menuPanel();
    lv_obj_t* grid = nullptr;
    for (uint32_t index = 0; index < lv_obj_get_child_count(parent); ++index)
    {
        auto* child = lv_obj_get_child(parent, index);
        if (child != dashboard_state().panel && lv_obj_get_style_flex_flow(child, LV_PART_MAIN) == LV_FLEX_FLOW_ROW_WRAP)
        {
            grid = child;
            break;
        }
    }
    if (!grid) return 3;
    const auto expected_children = lv_obj_get_child_count(parent);
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        auto* panel = dashboard_state().panel;
        lv_obj_delete(panel);
        if (dashboard_state().timer || dashboard_state().panel) return 4;
        lv_tick_inc(1100);
        lv_timer_handler();
        init(parent, grid, {});
        init(parent, grid, {}); // Repeated initialization must replace its owner.
        lv_obj_update_layout(parent);
        if (lv_obj_get_child_count(parent) != expected_children) return 8;
        if (native_dashboard_geometry() != 0) return 5;
        lv_tick_inc(1100);
        lv_timer_handler();
    }
    return 0;
}
