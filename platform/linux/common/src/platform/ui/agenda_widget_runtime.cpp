#include "platform/ui/agenda_widget_runtime.h"
namespace platform::ui
{
::ui::agenda::IAgendaSource* create_agenda_widget_source() { return nullptr; }
void destroy_agenda_widget_source(::ui::agenda::IAgendaSource*) {}
} // namespace platform::ui
