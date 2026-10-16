#pragma once

#include "platform/ui/orientation_runtime.h"

namespace boards::t_display_p4::heading_runtime
{

using HeadingState = ::platform::ui::orientation::HeadingState;

void ensure_started();
bool acquire();
void release();
HeadingState get_data();

} // namespace boards::t_display_p4::heading_runtime
