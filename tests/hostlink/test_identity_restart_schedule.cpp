#include <cassert>
#include <cstdint>
namespace
{
uint32_t clock_now = 100, s_web_restart_at = 0;
bool s_sync_pending = true;
uint32_t millis() { return clock_now; }
#include "identity_restart_schedule_actual.inc"
} // namespace
int main()
{
    scheduleWebManagementRestart();
    assert(s_web_restart_at == 2100 && !s_sync_pending);
    clock_now = 500;
    s_sync_pending = true;
    scheduleWebManagementRestart();
    assert(s_web_restart_at == 2100 && !s_sync_pending);
    s_web_restart_at = 0;
    clock_now = UINT32_MAX - 1999;
    scheduleWebManagementRestart();
    assert(s_web_restart_at == 1);
}
