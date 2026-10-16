#include "platform/esp/arduino_common/app_runtime_support.h"
#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/app_facades.h"
#include "ble/ble_manager.h"
#include "board/BoardBase.h"
#if defined(ARDUINO_T_LORA_PAGER)
#include "boards/tlora_pager/tlora_pager_board.h"
#endif
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/chat/infra/mesh_mqtt_client_runtime.h"
#include "platform/esp/arduino_common/device_identity.h"
#include "platform/esp/arduino_common/notification_runtime.h"
#include "platform/esp/arduino_common/reticulum_call_audio_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/wifi_access_runtime.h"
#include "sys/clock.h"
#include "sys/event_bus.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"
#include "ui/localization.h"
#include "ui/runtime/ui_feedback.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef APP_EVENT_LOG_ENABLE
#define APP_EVENT_LOG_ENABLE 0
#endif

#ifndef TRAIL_MATE_ENABLE_BLE
#define TRAIL_MATE_ENABLE_BLE 0
#endif

#if APP_EVENT_LOG_ENABLE
#define APP_EVENT_LOG(...) std::printf(__VA_ARGS__)
#else
#define APP_EVENT_LOG(...) \
    do                     \
    {                      \
    } while (0)
#endif

namespace platform::esp::arduino_common
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kContactAlertsKey = "chat_contact_alerts";
constexpr int kContactAlertsNone = 0;
constexpr int kContactAlertsContacts = 1;
constexpr int kContactAlertsAll = 2;
constexpr uint32_t kContactAlertModeCacheMs = 10000;
constexpr uint32_t kIncomingCallToneGapMs = 600;
constexpr uint32_t kIncomingCallToneStopTimeoutMs = 500;
constexpr uint32_t kIncomingCallToneTaskStackBytes = 5 * 1024;
constexpr UBaseType_t kIncomingCallToneTaskPriority = tskIDLE_PRIORITY + 1;

bool s_call_realtime_resources_held = false;
bool s_call_realtime_hooks_registered = false;
TaskHandle_t s_incoming_call_tone_task = nullptr;
volatile bool s_incoming_call_tone_stop_requested = false;

void pauseCallRealtimeResources()
{
    if (s_call_realtime_resources_held)
    {
        return;
    }
    ::platform::ui::gps::suspend_runtime();
    app::AppTasks::setRadioReceiveSuppressed(true);
    s_call_realtime_resources_held = true;
}

void resumeCallRealtimeResources()
{
    if (!s_call_realtime_resources_held)
    {
        return;
    }
    ::platform::ui::gps::resume_runtime();
    app::AppTasks::setRadioReceiveSuppressed(false);
    s_call_realtime_resources_held = false;
}

BoardBase* resolveCallBoard()
{
    const auto handles = ::platform::esp::boards::resolveAppContextInitHandles();
    return handles.board;
}

void playIncomingCallTone(BoardBase& board)
{
#if defined(ARDUINO_T_LORA_PAGER)
    static_cast<::boards::tlora_pager::TLoRaPagerBoard&>(board).playIncomingCallTone(
        &s_incoming_call_tone_stop_requested);
#else
    if (!s_incoming_call_tone_stop_requested)
    {
        board.playMessageTone();
    }
#endif
}

void incomingCallToneTask(void* argument)
{
    auto* board = static_cast<BoardBase*>(argument);
    while (!s_incoming_call_tone_stop_requested &&
           ::platform::ui::reticulum_call::realtime_phase() ==
               ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging)
    {
        playIncomingCallTone(*board);
        const TickType_t gap_started = xTaskGetTickCount();
        while (!s_incoming_call_tone_stop_requested &&
               ::platform::ui::reticulum_call::realtime_phase() ==
                   ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging &&
               pdTICKS_TO_MS(xTaskGetTickCount() - gap_started) <
                   kIncomingCallToneGapMs)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    s_incoming_call_tone_task = nullptr;
    vTaskDelete(nullptr);
}

bool startIncomingCallTone()
{
    if (s_incoming_call_tone_task)
    {
        return true;
    }
    BoardBase* board = resolveCallBoard();
    if (!board)
    {
        return false;
    }
    s_incoming_call_tone_stop_requested = false;
    const BaseType_t rc = xTaskCreate(incomingCallToneTask,
                                      "rt_call_ring",
                                      kIncomingCallToneTaskStackBytes,
                                      board,
                                      kIncomingCallToneTaskPriority,
                                      &s_incoming_call_tone_task);
    if (rc != pdPASS)
    {
        s_incoming_call_tone_task = nullptr;
        return false;
    }
    return true;
}

bool stopIncomingCallTone()
{
    s_incoming_call_tone_stop_requested = true;
    const TaskHandle_t task = s_incoming_call_tone_task;
    if (!task)
    {
        return true;
    }
    if (xTaskGetCurrentTaskHandle() == task)
    {
        return false;
    }

    const TickType_t started = xTaskGetTickCount();
    while (s_incoming_call_tone_task &&
           pdTICKS_TO_MS(xTaskGetTickCount() - started) <
               kIncomingCallToneStopTimeoutMs)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return s_incoming_call_tone_task == nullptr;
}

bool callRealtimeBeginSoftPreempt(
    const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    pauseCallRealtimeResources();
    return ::platform::ui::wifi_access::enter_call_ringing(link_id);
}

bool callRealtimeBeginRingingAlert(
    const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    (void)link_id;
    const bool started = startIncomingCallTone();
    if (!started)
    {
        std::printf("[Reticulum][CallTone] start failed\n");
    }
    return started;
}

bool callRealtimeBeginExclusive(const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    const bool was_ringing =
        ::platform::ui::reticulum_call::realtime_phase() ==
        ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging;
    if (!stopIncomingCallTone())
    {
        std::printf("[Reticulum][CallTone] stop timeout\n");
        return false;
    }
    if (!::platform::ui::wifi_access::enter_call_exclusive(link_id))
    {
        if (was_ringing)
        {
            (void)startIncomingCallTone();
        }
        return false;
    }
    pauseCallRealtimeResources();
    return true;
}

void callRealtimeBeginClosing(const uint8_t link_id[::platform::ui::reticulum_call::kHashSize],
                              bool keep_exclusive)
{
    (void)stopIncomingCallTone();
    pauseCallRealtimeResources();
    ::platform::ui::wifi_access::enter_call_closing(link_id, keep_exclusive);
}

void callRealtimeEnd(const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    (void)stopIncomingCallTone();
    ::platform::ui::wifi_access::exit_call(link_id);
    resumeCallRealtimeResources();
}

void ensureCallRealtimeHooksRegistered()
{
    if (s_call_realtime_hooks_registered)
    {
        return;
    }
    ::platform::ui::reticulum_call::RealtimeHooks hooks{};
    hooks.begin_soft_preempt = callRealtimeBeginSoftPreempt;
    hooks.begin_ringing_alert = callRealtimeBeginRingingAlert;
    hooks.begin_exclusive = callRealtimeBeginExclusive;
    hooks.begin_closing = callRealtimeBeginClosing;
    hooks.end = callRealtimeEnd;
    ::platform::ui::reticulum_call::set_realtime_hooks(hooks);
    s_call_realtime_hooks_registered = true;
}

bool applyCallRealtimeResourceGuard()
{
    ensureCallRealtimeHooksRegistered();
    const bool active = ::platform::ui::reticulum_call::resource_preempt_active() ||
                        ::platform::ui::wifi_access::call_soft_preempt_active();
    if (active)
    {
        pauseCallRealtimeResources();
    }
    else
    {
        resumeCallRealtimeResources();
    }
    return active;
}

int contactAlertMode()
{
    static int cached_mode = -1;
    static uint32_t cached_at_ms = 0;
    static bool cache_valid = false;

    const uint32_t now_ms = sys::millis_now();
    if (cache_valid &&
        cached_mode >= kContactAlertsNone &&
        cached_mode <= kContactAlertsAll &&
        (now_ms - cached_at_ms) < kContactAlertModeCacheMs)
    {
        return cached_mode;
    }

    int alert_mode = platform::ui::settings_store::get_int(kSettingsNs,
                                                           kContactAlertsKey,
                                                           kContactAlertsContacts);
    if (alert_mode < kContactAlertsNone || alert_mode > kContactAlertsAll)
    {
        alert_mode = kContactAlertsContacts;
    }

    cached_mode = alert_mode;
    cached_at_ms = now_ms;
    cache_valid = true;
    return cached_mode;
}

void triggerNodeInfoFeedback(app::IAppFacade& app_context)
{
    (void)notification::play_alert(app_context, notification::AlertKind::Contact);
}

std::string resolveNodeInfoName(app::IAppFacade& app_context, const sys::NodeInfoUpdateEvent& node_event)
{
    std::string name = app_context.getContactService().getContactName(node_event.node_id);
    if (!name.empty())
    {
        return name;
    }

    if (node_event.short_name[0] != '\0')
    {
        return std::string(node_event.short_name);
    }

    if (node_event.long_name[0] != '\0')
    {
        return std::string(node_event.long_name);
    }

    char fallback[16];
    std::snprintf(fallback, sizeof(fallback), "%08lX", static_cast<unsigned long>(node_event.node_id));
    return fallback;
}

void notifyNodeInfoUpdate(app::IAppFacade& app_context, const sys::NodeInfoUpdateEvent& node_event)
{
    if (node_event.node_id == 0 || node_event.node_id == app_context.getSelfNodeId() || node_event.is_ignored)
    {
        return;
    }

    const int alert_mode = contactAlertMode();
    if (alert_mode == kContactAlertsNone)
    {
        return;
    }

    const chat::contacts::PeerDirectoryItem* node_info =
        app_context.getContactService().getPeerByNodeId(node_event.node_id);
    const bool is_contact = node_info && node_info->is_contact;
    if (alert_mode == kContactAlertsContacts && !is_contact)
    {
        return;
    }

    triggerNodeInfoFeedback(app_context);

    const std::string message =
        ::ui::i18n::format("Node info: %s", resolveNodeInfoName(app_context, node_event).c_str());
    ::ui::feedback::show_notice(message.c_str(), 3000);
}

} // namespace

BackgroundTaskStartResult startBackgroundTasks(LoraBoard* board, chat::IMeshAdapter* adapter)
{
    ensureCallRealtimeHooksRegistered();
    ensureReticulumCallAudioRuntimeRegistered();
    if (!board)
    {
        return BackgroundTaskStartResult::NotSupported;
    }

    if (!app::AppTasks::init(*board, adapter))
    {
        return BackgroundTaskStartResult::Failed;
    }

    return BackgroundTaskStartResult::Started;
}

void tickBoundLifecycle(std::size_t max_events)
{
    app::IAppLifecycleFacade& lifecycle = app::lifecycleFacade();
    lifecycle.updateCoreServices();
    lifecycle.tickEventRuntime();
    lifecycle.dispatchPendingEvents(max_events);
}

void tickRuntime(app::IAppFacade& app_context)
{
    ensureCallRealtimeHooksRegistered();
    ensureReticulumCallAudioRuntimeRegistered();
    if (applyCallRealtimeResourceGuard())
    {
        return;
    }
    mesh_mqtt::update(app_context);

#if TRAIL_MATE_ENABLE_BLE
    if (ble::BleManager* ble_manager = app_context.getBleManager())
    {
        ble_manager->update();
    }
#else
    (void)app_context;
#endif
}

void updateCoreServices(app::IAppFacade& app_context)
{
    ensureCallRealtimeHooksRegistered();
    if (::platform::ui::reticulum_call::resource_preempt_active())
    {
        return;
    }

    platform::ui::tracker::poll();

    app_context.getChatService().processIncoming();
    app_context.getChatService().flushStore();

    team::TeamService* team_service = app_context.getTeamService();
    if (team_service)
    {
        team_service->processIncoming();
    }

    team::TeamPairingService* team_pairing = app_context.getTeamPairing();
    if (team_pairing)
    {
        team_pairing->update();
    }

    const bool team_active = team_service && team_service->hasKeys();
    app_context.setTeamModeActive(team_active);

    team::TeamTrackSampler* team_track_sampler = app_context.getTeamTrackSampler();
    if (team_track_sampler)
    {
        team_track_sampler->update(app_context.getTeamController(), team_active);
    }
}

bool dispatchEvent(app::IAppFacade& app_context, sys::Event* event)
{
    if (!event)
    {
        return true;
    }

    switch (event->type)
    {
    case sys::EventType::ChatSendResult:
    {
        auto* result_event = static_cast<sys::ChatSendResultEvent*>(event);
        if (result_event->has_protocol)
        {
            app_context.getChatService().handleSendResultForProtocol(
                result_event->msg_id,
                result_event->protocol,
                result_event->status,
                result_event->timestamp,
                result_event->failure);
        }
        else
        {
            app_context.getChatService().handleSendResult(result_event->msg_id,
                                                          result_event->status,
                                                          result_event->timestamp,
                                                          result_event->failure);
        }
        return false;
    }
    case sys::EventType::NodeInfoUpdate:
    {
        auto* node_event = static_cast<sys::NodeInfoUpdateEvent*>(event);
        chat::contacts::NodeUpdate update{};
        update.short_name = node_event->short_name;
        update.long_name = node_event->long_name;
        update.has_last_seen = true;
        update.last_seen = node_event->timestamp;
        update.has_snr = true;
        update.snr = node_event->snr;
        update.has_rssi = true;
        update.rssi = node_event->rssi;
        update.has_protocol = true;
        update.protocol = node_event->protocol;
        update.has_role = true;
        update.role = node_event->role;
        update.has_hops_away = true;
        update.hops_away = node_event->hops_away;
        update.has_hw_model = true;
        update.hw_model = node_event->hw_model;
        update.has_channel = true;
        update.channel = node_event->channel;
        update.has_macaddr = node_event->has_macaddr;
        if (node_event->has_macaddr)
        {
            memcpy(update.macaddr, node_event->macaddr, sizeof(update.macaddr));
        }
        update.has_via_mqtt = true;
        update.via_mqtt = node_event->via_mqtt;
        update.has_is_ignored = true;
        update.is_ignored = node_event->is_ignored;
        update.has_public_key = node_event->has_public_key_state;
        update.public_key_present = node_event->has_public_key;
        update.has_key_manually_verified = node_event->has_key_manually_verified_state;
        update.key_manually_verified = node_event->key_manually_verified;
        update.reticulum_identity = node_event->reticulum_identity;
        update.has_device_metrics = node_event->has_device_metrics;
        if (node_event->has_device_metrics)
        {
            update.device_metrics = node_event->device_metrics;
        }
        APP_EVENT_LOG("[APP][event] node_info node=%08lX proto=%u role=%u hops=%u short='%s' long='%s' snr=%.1f rssi=%.1f ts=%lu ignored=%u pubkey=%u\n",
                      static_cast<unsigned long>(node_event->node_id),
                      static_cast<unsigned>(node_event->protocol),
                      static_cast<unsigned>(node_event->role),
                      static_cast<unsigned>(node_event->hops_away),
                      node_event->short_name,
                      node_event->long_name,
                      static_cast<double>(node_event->snr),
                      static_cast<double>(node_event->rssi),
                      static_cast<unsigned long>(node_event->timestamp),
                      node_event->is_ignored ? 1U : 0U,
                      node_event->has_public_key ? 1U : 0U);
        app_context.getContactService().applyNodeUpdate(node_event->node_id, update);
        notifyNodeInfoUpdate(app_context, *node_event);
        delete event;
        return true;
    }
    case sys::EventType::NodeProtocolUpdate:
    {
        auto* node_event = static_cast<sys::NodeProtocolUpdateEvent*>(event);
        app_context.getContactService().updateNodeProtocol(
            node_event->node_id,
            node_event->protocol,
            node_event->timestamp);
        delete event;
        return true;
    }
    case sys::EventType::NodePositionUpdate:
    {
        auto* pos_event = static_cast<sys::NodePositionUpdateEvent*>(event);
        chat::contacts::NodePosition pos{};
        pos.valid = true;
        pos.latitude_i = pos_event->latitude_i;
        pos.longitude_i = pos_event->longitude_i;
        pos.has_altitude = pos_event->has_altitude;
        pos.altitude = pos_event->altitude;
        pos.timestamp = pos_event->timestamp;
        pos.precision_bits = pos_event->precision_bits;
        pos.pdop = pos_event->pdop;
        pos.hdop = pos_event->hdop;
        pos.vdop = pos_event->vdop;
        pos.gps_accuracy_mm = pos_event->gps_accuracy_mm;
        APP_EVENT_LOG("[APP][event] node_position node=%08lX lat_i=%ld lon_i=%ld alt=%ld has_alt=%u ts=%lu\n",
                      static_cast<unsigned long>(pos_event->node_id),
                      static_cast<long>(pos_event->latitude_i),
                      static_cast<long>(pos_event->longitude_i),
                      static_cast<long>(pos_event->altitude),
                      pos_event->has_altitude ? 1U : 0U,
                      static_cast<unsigned long>(pos_event->timestamp));
        app_context.getContactService().updateNodePosition(pos_event->node_id, pos);
        delete event;
        return true;
    }
    default:
        return false;
    }
}

std::unique_ptr<ble::BleManager> createBleManager(app::IAppBleFacade& app_facade)
{
#if TRAIL_MATE_ENABLE_BLE
    std::unique_ptr<ble::BleManager> ble_manager(new ble::BleManager(app_facade));
    if (app_facade.readConfig().ble_enabled &&
        !mesh_mqtt::wantsStandaloneMode(app_facade))
    {
        ble_manager->setEnabled(true);
    }
    else if (app_facade.readConfig().ble_enabled)
    {
        std::printf("[MQTT] BLE startup skipped for standalone mesh MQTT mode\n");
    }
    return ble_manager;
#else
    (void)app_facade;
    return std::unique_ptr<ble::BleManager>();
#endif
}

} // namespace platform::esp::arduino_common
