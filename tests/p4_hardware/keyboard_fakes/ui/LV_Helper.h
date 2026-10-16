#pragma once

struct LilyGo_Display
{
};
struct lv_group_t
{
};
enum lv_indev_type_t
{
    LV_INDEV_TYPE_POINTER,
    LV_INDEV_TYPE_KEYPAD,
    LV_INDEV_TYPE_ENCODER
};
struct lv_indev_t
{
    lv_indev_type_t type;
    lv_indev_t* next;
};
extern lv_indev_t* devices;
inline lv_indev_t* lv_indev_get_next(lv_indev_t* current) { return current ? current->next : devices; }
inline lv_indev_type_t lv_indev_get_type(lv_indev_t* device) { return device->type; }
inline void lv_group_set_default(lv_group_t*) {}
