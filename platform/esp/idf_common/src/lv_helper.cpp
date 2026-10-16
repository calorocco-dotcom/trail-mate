#include "ui/LV_Helper.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "bsp/trail_mate_t_display_p4_keyboard.h"
#endif

namespace
{

lv_indev_t* find_indev(lv_indev_type_t type)
{
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev))
    {
        if (lv_indev_get_type(indev) == type)
        {
            return indev;
        }
    }
    return nullptr;
}

} // namespace

void beginLvglHelper(LilyGo_Display& display, bool debug)
{
    (void)display;
    (void)debug;
}

void lv_set_default_group(lv_group_t* group)
{
    lv_group_set_default(group);
}

lv_indev_t* lv_get_touch_indev()
{
    return find_indev(LV_INDEV_TYPE_POINTER);
}

lv_indev_t* lv_get_keyboard_indev()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // The adapter remains registered while an optional keyboard is absent so
    // it can retry discovery. Editors must still offer the touch keyboard.
    if (!trail_mate_t_display_p4_keyboard_is_ready())
    {
        return nullptr;
    }
#endif
    return find_indev(LV_INDEV_TYPE_KEYPAD);
}

lv_indev_t* lv_get_encoder_indev()
{
    return find_indev(LV_INDEV_TYPE_ENCODER);
}

bool lv_begin_external_font_load_fs_scope()
{
    return true;
}

void lv_end_external_font_load_fs_scope()
{
}

bool lv_external_font_load_fs_was_busy()
{
    return false;
}
