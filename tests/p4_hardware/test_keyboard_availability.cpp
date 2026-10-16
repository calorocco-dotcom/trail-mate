#include "ui/LV_Helper.h"
#include <cassert>

lv_indev_t* devices = nullptr;
bool keyboard_ready = false;
extern "C" bool trail_mate_t_display_p4_keyboard_is_ready() { return keyboard_ready; }

#include "../../platform/esp/idf_common/src/lv_helper.cpp"

int main()
{
    lv_indev_t keyboard{LV_INDEV_TYPE_KEYPAD, nullptr};
    lv_indev_t touch{LV_INDEV_TYPE_POINTER, &keyboard};
    devices = &touch;
    assert(lv_get_touch_indev() == &touch);
    // A registered recovery adapter is not evidence of attached hardware.
    assert(lv_get_keyboard_indev() == nullptr);
    keyboard_ready = true;
    assert(lv_get_keyboard_indev() == &keyboard);
    keyboard_ready = false;
    assert(lv_get_keyboard_indev() == nullptr);
    keyboard_ready = true;
    touch.next = nullptr;
    assert(lv_get_keyboard_indev() == nullptr);
    assert(lv_get_touch_indev() == &touch);
}
