#include "ui/menu/dashboard/dashboard_state.h"

namespace ui::menu::dashboard
{
namespace
{
DashboardUi* s_dashboard = nullptr;
}
DashboardUi* dashboard_state() { return s_dashboard; }
bool dashboard_create()
{
    s_dashboard = create_widget_state<DashboardUi>();
    return s_dashboard != nullptr;
}
void dashboard_reset()
{
    if (!s_dashboard) return;
    if (s_dashboard->timer) lv_timer_delete(s_dashboard->timer);
    delete_widget_state(s_dashboard);
    s_dashboard = nullptr;
}
} // namespace ui::menu::dashboard
