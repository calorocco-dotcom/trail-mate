#pragma once
namespace ui::agenda
{
class IAgendaSource;
}
namespace platform::ui
{
// Optional per-view adapter for targets without a resident Calendar composition.
// The creator owns the result and must release it when the widget leaves.
::ui::agenda::IAgendaSource* create_agenda_widget_source();
void destroy_agenda_widget_source(::ui::agenda::IAgendaSource* source);
} // namespace platform::ui
