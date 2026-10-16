#pragma once

#include "lvgl.h"

#include "ui/menu/menu_layout.h"

namespace ui::agenda
{
class IAgendaSource;
}

namespace ui::menu::dashboard
{

#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2) || defined(ARDUINO_T_LORA_PAGER)
void init(lv_obj_t* menu_panel, lv_obj_t* grid_panel, const ui::menu_layout::InitOptions& options);
void bringToFront();
void refresh_localized_text();
void setActive(bool active);
// Target-owned read port; its lifetime must exceed the menu's lifetime.
void bindAgendaSource(ui::agenda::IAgendaSource* source);
bool isFeedOpen();
bool handleKey(uint32_t key, bool at_left_edge = false);
bool handleEncoder(int delta);
#else
inline void init(lv_obj_t* menu_panel, lv_obj_t* grid_panel, const ui::menu_layout::InitOptions& options)
{
    (void)menu_panel;
    (void)grid_panel;
    (void)options;
}

inline void bringToFront()
{
}

inline void refresh_localized_text()
{
}

inline void setActive(bool active)
{
    (void)active;
}
inline void bindAgendaSource(ui::agenda::IAgendaSource*) {}
inline bool isFeedOpen() { return false; }
inline bool handleKey(uint32_t, bool = false) { return false; }
inline bool handleEncoder(int) { return false; }
#endif

} // namespace ui::menu::dashboard
