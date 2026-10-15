#pragma once

#include "lvgl.h"

namespace ui::components::dialog_frame
{
struct Config
{
    const char* title;
    lv_coord_t width = 304;
    lv_coord_t height = 180;
    lv_event_cb_t on_close = nullptr;
    lv_event_cb_t on_key = nullptr;
    void* context = nullptr;
};

struct Frame
{
    lv_obj_t* overlay;
    lv_obj_t* panel;
    lv_obj_t* body;
    lv_obj_t* close;
};

// The page owns the returned widgets and focus group. No retained controller or
// business data: the fixed title bar and scrolling body are just shared chrome.
Frame create(lv_obj_t* parent, const Config& config);
} // namespace ui::components::dialog_frame
