#pragma once
#include "lvgl.h"
namespace music
{
class IService;
}
namespace ui::music_page
{
void bind_service(::music::IService* service);
void enter(void* host, lv_obj_t* parent);
void exit(void* host, lv_obj_t* parent);
} // namespace ui::music_page
