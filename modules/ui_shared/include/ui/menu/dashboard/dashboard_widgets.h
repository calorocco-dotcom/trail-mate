#pragma once
#include "ui/menu/dashboard/dashboard_widget.h"

namespace ui::agenda
{
class IAgendaSource;
}
namespace ui::menu::dashboard
{
WidgetHandle create_mesh_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_gps_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_recent_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_compass_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_agenda_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, ui::agenda::IAgendaSource* source);
} // namespace ui::menu::dashboard
