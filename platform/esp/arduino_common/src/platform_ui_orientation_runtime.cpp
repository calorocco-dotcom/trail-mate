#include "platform/ui/orientation_runtime.h"

namespace platform::ui::orientation
{
bool acquire_heading() { return false; }
void release_heading() {}
// These targets expose GPS/team bearings but have no heading-sensor service.
// Do not create a polling task or imply that an absent magnetometer is warming up.
HeadingState get_heading() { return {}; }
void ensure_heading_runtime() {}
ScreenOrientationState get_screen_orientation() { return {}; }
void ensure_screen_orientation_runtime() {}
} // namespace platform::ui::orientation
