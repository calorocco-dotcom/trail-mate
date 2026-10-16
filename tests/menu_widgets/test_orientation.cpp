#include "platform/ui/orientation_runtime.h"
#include <cassert>
int main()
{
    namespace orientation = platform::ui::orientation;
    assert(!orientation::heading_supported());
    orientation::ensure_heading_runtime();
    orientation::ensure_screen_orientation_runtime();
    const auto heading = orientation::get_heading();
    assert(!heading.available && !heading.sensor_ready);
    const auto screen = orientation::get_screen_orientation();
    assert(!screen.sensor_available);
    assert(screen.policy == orientation::ScreenOrientationPolicy::LandscapeLocked);
    assert(screen.active_orientation == orientation::ScreenOrientation::Landscape);
}
