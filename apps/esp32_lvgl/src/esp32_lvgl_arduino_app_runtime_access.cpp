#include "esp32_lvgl_arduino_app_runtime_access.h"

#include <Arduino.h>

#include "app/app_context.h"
#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"
#include "platform/esp/arduino_common/app_context_platform_bindings.h"
#include "platform/esp/arduino_common/app_runtime_bootstrap_support.h"
#include "platform/esp/arduino_common/app_runtime_support.h"
#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
#include "platform/ui/a7682e_cellular_runtime.h"
#include "platform/ui/a7682e_cellular_screen_port.h"
#endif
#if defined(ARDUINO_T_DECK_PRO)
#include "platform/ui/screen_240x320_protocol_probe_port.h"
#include "platform/ui/screen_240x320_runtime_feature_port.h"
#endif
#include "platform/ui/screen_runtime.h"
#if defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "usb/usb_cdc_transport.h"
#endif

namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
{
namespace
{

Status s_status{};
bool s_was_screen_saver_active = false;
uint32_t s_wake_gate_until_ms = 0;

bool wake_gate_active(uint32_t now_ms)
{
    return s_wake_gate_until_ms != 0 &&
           static_cast<int32_t>(s_wake_gate_until_ms - now_ms) > 0;
}

} // namespace

ConfigurationPreloadResult preloadConfiguration()
{
    if (s_status.initialized)
    {
        return s_status.app_context_bound ? ConfigurationPreloadResult::Ready
                                          : ConfigurationPreloadResult::Failed;
    }

    app::AppContext& app_context = app::AppContext::getInstance();
    app_context.configurePlatformBindings(
        platform::esp::arduino_common::makeAppContextPlatformBindings());
    if (app_context.preloadConfig())
    {
        return ConfigurationPreloadResult::Ready;
    }
    return app::sd_tms::workingConfigRequiresRepair()
               ? ConfigurationPreloadResult::RepairRequired
               : ConfigurationPreloadResult::Failed;
}

bool initialize(bool use_mock)
{
    if (s_status.initialized)
    {
        return s_status.app_context_bound;
    }

    s_status = Status{};
    s_status.initialized = true;

    const auto handles = platform::esp::boards::resolveAppContextInitHandles();
    app::AppContext& app_context = app::AppContext::getInstance();
    platform::esp::arduino_common::AppContextBootstrapResult bootstrap_result{};
    s_status.board_handles_ready = handles.isValid();
    if (!s_status.board_handles_ready)
    {
        Serial.printf("[APP] ERROR: board runtime returned invalid AppContext handles\n");
        return false;
    }

    if (!platform::esp::arduino_common::bootstrapAppContext(app_context, handles, use_mock, &bootstrap_result))
    {
        return false;
    }

    s_status.app_context_bound = bootstrap_result.app_context_bound;

#if defined(ARDUINO_T_DECK_PRO)
    ::platform::ui::install_screen_240x320_runtime_feature_port();
    ::platform::ui::install_screen_240x320_protocol_probe_port();
#endif

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    ::platform::ui::a7682e::install_screen_240x320_port();
#endif

    switch (bootstrap_result.background_tasks)
    {
    case platform::esp::arduino_common::BackgroundTaskStartResult::NotSupported:
        Serial.printf("[APP] WARNING: Board type not supported for LoRa tasks\n");
        return true;

    case platform::esp::arduino_common::BackgroundTaskStartResult::Failed:
        Serial.printf("[APP] WARNING: Failed to start LoRa tasks\n");
        return true;

    case platform::esp::arduino_common::BackgroundTaskStartResult::Started:
        s_status.background_tasks_started = true;
        Serial.printf("[APP] LoRa tasks started\n");
        return true;
    }

    return true;
}

void startDeferredStorage()
{
    if (!s_status.initialized || !s_status.app_context_bound)
    {
        return;
    }
    app::AppContext::getInstance().startDeferredStorage();
}

void tick()
{
    if (!s_status.app_context_bound)
    {
        return;
    }
    platform::esp::arduino_common::storage::tick_deferred_storage();
#if defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    ::hostlink::tick_publication_tasks();
    // Enumerate CDC without allocating HostLink queues/task until a host opens
    // it. Run command processing before saver gates, just like the IDF loop.
    if (usb_cdc::start())
    {
        static uint32_t last_start_attempt = 0;
        if (usb_cdc::is_connected())
        {
            const uint32_t current = millis();
            if (!::hostlink::is_active() && current - last_start_attempt >= 1000U)
            {
                last_start_attempt = current;
                ::hostlink::start();
            }
            ::hostlink::process_pending_commands();
        }
        else if (::hostlink::is_active())
        {
            ::hostlink::process_pending_commands();
            ::hostlink::stop();
        }
    }
#endif
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    // Receive call/SMS URCs even while the EPD saver owns the foreground UI.
    ::platform::ui::a7682e::tick();
#endif
    const uint32_t now_ms = millis();
    const bool saver_active = platform::ui::screen::is_saver_active();
    if (saver_active)
    {
        s_was_screen_saver_active = true;
        return;
    }
    if (s_was_screen_saver_active)
    {
        s_was_screen_saver_active = false;
        s_wake_gate_until_ms = now_ms + 750U;
        return;
    }
    if (wake_gate_active(now_ms))
    {
        return;
    }

    platform::esp::arduino_common::tickBoundLifecycle();
}

const Status& status()
{
    return s_status;
}

} // namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
