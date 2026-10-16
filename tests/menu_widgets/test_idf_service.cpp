#include "app/app_facade_access.h"
#include "platform/esp/idf_common/app_runtime_support.h"
#include <cassert>
#include <cstdint>

namespace
{
bool allow_display = true, display_locked = false, pending_control = false;
unsigned controls_applied = 0, network_ticks = 0, events = 0;
app::IAppLifecycleFacade lifecycle;
} // namespace
namespace app
{
bool hasAppFacade() { return true; }
IAppLifecycleFacade& lifecycleFacade() { return lifecycle; }
void IAppLifecycleFacade::updateCoreServices()
{
    assert(!display_locked);
    ++network_ticks;
    pending_control = true;
}
void IAppLifecycleFacade::tickEventRuntime() { assert(display_locked); }
void IAppLifecycleFacade::dispatchPendingEvents(std::size_t count)
{
    assert(display_locked && count <= 4);
    ++events;
}
namespace sd_tms
{
void serviceWorkingConfig() { assert(display_locked); }
} // namespace sd_tms
} // namespace app
namespace platform::esp::boards
{
bool lockDisplay(uint32_t)
{
    assert(!display_locked);
    display_locked = allow_display;
    return display_locked;
}
void unlockDisplay()
{
    assert(display_locked);
    display_locked = false;
}
} // namespace platform::esp::boards
namespace hostlink
{
void tick_publication_tasks() { assert(!display_locked); }
void process_pending_commands() { assert(display_locked); }
} // namespace hostlink
namespace platform::ui
{
void service_quick_controls()
{
    assert(!display_locked);
    if (pending_control) ++controls_applied;
    pending_control = false;
}
} // namespace platform::ui
int main()
{
    using namespace platform::esp::idf_common;
    tickBoundLifecycle();
    assert(network_ticks == 1 && controls_applied == 1 && events == 1);
    // UI dispatch moving into the LVGL task must not disable the control pump.
    setLvglTaskOwnedUiDispatch(true);
    tickBoundLifecycle();
    assert(network_ticks == 2 && controls_applied == 2 && events == 1);
    // Display contention must not drop a brightness/volume intent or stall it.
    allow_display = false;
    tickBoundLifecycle();
    assert(network_ticks == 3 && controls_applied == 3 && !pending_control);
    setLvglTaskOwnedUiDispatch(false);
    tickBoundLifecycle();
    assert(network_ticks == 4 && controls_applied == 4 && events == 1);
    allow_display = true;
    tickBoundLifecycle(100);
    assert(network_ticks == 5 && controls_applied == 5 && events == 2);
}
