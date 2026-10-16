// The Agenda composition is tested independently from menu rendering.
namespace ui::agenda
{
class IAgendaSource;
}
namespace ui::menu::dashboard
{
void bindAgendaSource(ui::agenda::IAgendaSource*) {}
} // namespace ui::menu::dashboard
