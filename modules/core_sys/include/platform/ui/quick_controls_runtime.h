#pragma once
namespace ui::controls
{
class IQuickControls;
}
namespace platform::ui
{
::ui::controls::IQuickControls* quick_controls_source();
// Pump bounded user requests from the existing application service loop, outside
// LVGL. Reading a widget never starts this work or creates another RTOS task.
void service_quick_controls();
} // namespace platform::ui
