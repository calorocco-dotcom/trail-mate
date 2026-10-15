#define TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4 1
#include "../../boards/t_display_p4/src/heading_runtime.cpp"
#include <cassert>
#include <iostream>
int main()
{
    using namespace boards::t_display_p4::heading_runtime;
    fake::now_us = 1000000;
    assert(ensure_sensor_ready());
    // Temperature bit 3 is not HOFL. ST2 is the ninth byte and MUST be read.
    assert(sample_once());
    assert(fake::last_sample_size == 9);
    assert(get_data().available && get_data().raw_x == 100);
    fake::sample[8] = 8;
    assert(!sample_once());
    fake::now_us += 1600000;
    mark_stale_if_needed();
    assert(!g_runtime.sensor_ready && !get_data().available);
    assert(ensure_sensor_ready());
    // A freshly initialized device which never produces data also recovers.
    fake::now_us += 1600000;
    mark_stale_if_needed();
    assert(!g_runtime.sensor_ready);
    fake::sample[8] = 0;
    assert(ensure_sensor_ready() && sample_once());
    assert(get_data().available);
    std::cout << "P4 magnetometer ST2, overflow and stale recovery passed\n";
}
