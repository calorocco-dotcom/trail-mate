#pragma once
#include "ui/menu/dashboard/dashboard_widget.h"

namespace ui::agenda
{
class IAgendaSource;
}
namespace ui::controls
{
class IQuickControls;
}
namespace music
{
class IService;
}
namespace ui::menu::dashboard
{
WidgetHandle create_mesh_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_gps_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_recent_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_compass_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
WidgetHandle create_agenda_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, ui::agenda::IAgendaSource* source);
WidgetHandle create_controls_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, ui::controls::IQuickControls* source);
WidgetHandle create_music_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, ::music::IService* source);
} // namespace ui::menu::dashboard
