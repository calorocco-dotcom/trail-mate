#pragma once
#include "lvgl.h"
#include "ui_presentation/map/map_overlay_snapshot.h"
namespace ui::support
{
// Stream projected segments directly from the existing snapshot. No retained
// vertex list, per-segment LVGL objects or connection across failed projections.
template <class Project, class Emit>
void visitOverlayPathSegments(const ui::map::MapOverlaySnapshot& snapshot, Project&& project, Emit&& emit)
{
    bool previous = false;
    lv_point_t last{};
    ui::map::MapOverlayStyle style = ui::map::MapOverlayStyle::Default;
    for (size_t i = 0; i < snapshot.item_count && i < ui::map::MapOverlaySnapshot::kMaxItems; ++i)
    {
        const auto& item = snapshot.items[i];
        if (!(item.path_flags & 1))
        {
            previous = false;
            continue;
        }
        if (item.path_flags & 2) previous = false;
        lv_point_t current{};
        if (!item.visible || !item.point.valid || !project(item.point, current))
        {
            previous = false;
            continue;
        }
        if (previous && style == item.style) emit(last, current, item.style);
        last = current;
        style = item.style;
        previous = true;
    }
}
} // namespace ui::support
