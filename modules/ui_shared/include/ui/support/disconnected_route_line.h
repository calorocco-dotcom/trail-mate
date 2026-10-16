#pragma once
#include "lvgl.h"
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ui::support
{
// Marker is read only by our draw callback, never passed to lv_line_set_points.
// It fits both integer and floating lv_value_precise_t exactly.
constexpr int32_t kRouteLineBreak = std::numeric_limits<int32_t>::min();
inline lv_point_precise_t routeLineBreak() { return {kRouteLineBreak, kRouteLineBreak}; }
inline void drawDisconnectedRouteLine(lv_event_t* event, const lv_point_precise_t* points, size_t count)
{
    if (!points || count < 2) return;
    auto* object = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    auto* layer = lv_event_get_layer(event);
    if (!object || !layer) return;
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const auto x = area.x1 - lv_obj_get_scroll_x(object), y = area.y1 - lv_obj_get_scroll_y(object);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.base.layer = layer;
    lv_obj_init_draw_line_dsc(object, LV_PART_MAIN, &line);
    bool previous = false;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& point = points[i];
        if (point.x == kRouteLineBreak && point.y == kRouteLineBreak)
        {
            previous = false;
            continue;
        }
        line.p2 = {point.x + x, point.y + y};
        if (previous) lv_draw_line(layer, &line);
        line.p1 = line.p2;
        previous = true;
    }
}
} // namespace ui::support
