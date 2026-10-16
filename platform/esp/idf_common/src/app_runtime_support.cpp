#include "platform/esp/idf_common/app_runtime_support.h"

#include "app/app_facade_access.h"
#include "esp_log.h"
#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/ui/quick_controls_runtime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace
{

constexpr const char* kTag = "idf-app-runtime";
constexpr uint32_t kDisplayLockTimeoutMs = 50;
constexpr std::size_t kMaxEventsPerDisplayLock = 4;

std::atomic<bool> s_lvgl_task_owned_ui_dispatch{false};

} // namespace

namespace platform::esp::idf_common
{

void setLvglTaskOwnedUiDispatch(bool enabled)
{
    s_lvgl_task_owned_ui_dispatch.store(enabled, std::memory_order_release);
}

void tickLvglTaskOwnedUiLifecycle(std::size_t max_events)
{
    if (!app::hasAppFacade())
    {
        return;
    }

    app::IAppLifecycleFacade& lifecycle = app::lifecycleFacade();
    lifecycle.tickEventRuntime();

    const std::size_t event_budget =
        max_events < kMaxEventsPerDisplayLock ? max_events : kMaxEventsPerDisplayLock;
    lifecycle.dispatchPendingEvents(event_budget);
}

void tickBoundLifecycle(std::size_t max_events)
{
    static uint32_t consecutive_lock_timeouts = 0;

    app::IAppLifecycleFacade& lifecycle = app::lifecycleFacade();

    // Keep the network/storage service pump outside the LVGL mutex. Reticulum
    // Wi-Fi ingress can process many packets while no app page is visible, and
    // holding the display lock for that work starves the LVGL render/touch
    // task on ESP-IDF targets. Only the actual UI projection below needs the
    // display lock.
    lifecycle.updateCoreServices();
    // Consume UI intents on the existing service task, outside the LVGL lock.
    // Arduino pumps this from AppContext; native IDF has its own lifecycle.
    ::platform::ui::service_quick_controls();
    ::hostlink::tick_publication_tasks();
    // Configuration emission reads the live object and platform settings.
    // Serialize it with UI edits without allocating a second AppConfig. The
    // network pump above remains outside this mutex. Bound work to one command.
    if (platform::esp::boards::lockDisplay(kDisplayLockTimeoutMs))
    {
        ::hostlink::process_pending_commands();
        app::sd_tms::serviceWorkingConfig();
        platform::esp::boards::unlockDisplay();
    }

    if (s_lvgl_task_owned_ui_dispatch.load(std::memory_order_acquire))
    {
        return;
    }

    if (!platform::esp::boards::lockDisplay(kDisplayLockTimeoutMs))
    {
        ++consecutive_lock_timeouts;
        if (consecutive_lock_timeouts == 1 ||
            (consecutive_lock_timeouts % 128U) == 0U)
        {
            ESP_LOGW(kTag,
                     "LVGL lifecycle lock timeout wait_ms=%lu consecutive=%lu; events retained",
                     static_cast<unsigned long>(kDisplayLockTimeoutMs),
                     static_cast<unsigned long>(consecutive_lock_timeouts));
        }
        return;
    }

    if (consecutive_lock_timeouts != 0)
    {
        ESP_LOGI(kTag,
                 "LVGL lifecycle lock recovered after=%lu",
                 static_cast<unsigned long>(consecutive_lock_timeouts));
        consecutive_lock_timeouts = 0;
    }

    lifecycle.tickEventRuntime();

    const std::size_t event_budget =
        max_events < kMaxEventsPerDisplayLock ? max_events : kMaxEventsPerDisplayLock;
    lifecycle.dispatchPendingEvents(event_budget);
    platform::esp::boards::unlockDisplay();
}

} // namespace platform::esp::idf_common
