#define TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4 1
#include "../../boards/t_display_p4/src/haptic_runtime.cpp"
#include <cassert>
#include <iostream>
int main()
{
    using namespace boards::t_display_p4::haptic_runtime;
    assert(trigger() && fake::resets == 1);
    fake::i2c_ok = false;
    assert(!trigger() && !g_runtime.ready);
    fake::i2c_ok = true;
    assert(trigger() && fake::resets == 2);
    fake::i2c_ok = false;
    stop();
    assert(!g_runtime.ready);
    fake::i2c_ok = true;
    assert(trigger() && fake::resets == 3);
    std::cout << "P4 haptic reinitializes after failed playback/stop transfers\n";
}
