#pragma once
#include "ui/menu/dashboard/dashboard_widget.h"
#include <array>

namespace ui::menu::dashboard
{
// Container state only. No child controls or service data belong here.
// This owner is allocated in PSRAM; the registry retains one pointer.
struct DashboardUi
{
    lv_obj_t* panel = nullptr;
    lv_obj_t* grid = nullptr;
    lv_obj_t* menu_grid = nullptr;
    lv_obj_t* pager = nullptr;
    lv_timer_t* timer = nullptr;
    std::array<WidgetHandle, 4> widgets{};
    uint32_t tick = 0;
    bool dock_right = false;
    bool feed_open = false;
    bool active = true;
    uint8_t page = 0;
    lv_indev_t* pointer = nullptr;
    lv_point_t start{};
    bool pressed = false;
    bool inside_dashboard = false;
};

DashboardUi* dashboard_state();
bool dashboard_create();
void dashboard_reset();
} // namespace ui::menu::dashboard
