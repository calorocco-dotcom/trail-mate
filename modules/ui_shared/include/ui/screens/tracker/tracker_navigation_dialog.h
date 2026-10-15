#pragma once
#include "lvgl.h"

namespace tracker::ui::navigation_dialog
{
using Completed = void (*)(const char* path, void* context);
bool open(lv_obj_t* parent, Completed completed, void* context = nullptr);
bool is_open();
void close();
} // namespace tracker::ui::navigation_dialog
