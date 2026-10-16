#pragma once

#include "app/app_facade_access.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/esp/common/wifi_profile_snapshot.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_access_runtime.h"
#include "platform/ui/wifi_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef TRAIL_MATE_ENABLE_BLE
#define TRAIL_MATE_ENABLE_BLE 0
#endif

#if TRAIL_MATE_ENABLE_BLE && __has_include("ble/ble_manager.h")
#include "ble/ble_manager.h"
#define TRAIL_MATE_WIFI_HAS_BLE_MANAGER 1
#else
#define TRAIL_MATE_WIFI_HAS_BLE_MANAGER 0
#endif

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "bsp/trail_mate_tab5_runtime.h"
#endif

namespace platform::ui::wifi
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr const char* kWifiProfileCountKey = "wifi_prof_count";
constexpr std::size_t kWifiProfileCapacity = ::platform::ui::wifi::kMaxSavedProfileCount;
constexpr uint16_t kWifiAutoScanMaxRecords = 16;
constexpr uint32_t kBleRetryDelayMs = 180;
constexpr int kWifiTxBufferTypeStatic = 0;
constexpr int kWifiTxBufferTypeDynamic = 1;
constexpr int kWifiPrimaryStaticRxBufNum = 4;
constexpr int kWifiPrimaryDynamicRxBufNum = 16;
constexpr int kWifiPrimaryDynamicTxBufNum = 16;
constexpr int kWifiPrimaryCacheTxBufNum = 4;
constexpr int kWifiPrimaryMgmtSbufNum = 8;
constexpr int kWifiRetryStaticRxBufNum = 4;
constexpr int kWifiRetryDynamicRxBufNum = 8;
constexpr int kWifiRetryDynamicTxBufNum = 8;
constexpr int kWifiRetryCacheTxBufNum = 4;
constexpr int kWifiRetryMgmtSbufNum = 6;
constexpr int kWifiRetryRxMgmtBufNum = 5;
constexpr std::size_t kWifiConnectMinInternalFreeBytes = 32U * 1024U;
constexpr std::size_t kWifiConnectMinInternalLargestBlockBytes = 8U * 1024U;
constexpr std::size_t kWifiReconnectReserveBytes = 32U * 1024U;
constexpr const char* kNetworkTimeSyncServer = "pool.ntp.org";
constexpr std::time_t kNetworkTimeMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC
constexpr uint32_t kNetworkTimeSyncTimeoutMs = 15000;
constexpr uint32_t kNetworkTimeApplyTaskStackBytes = 3072;
constexpr uint32_t kWifiProfileRetryDelayMs = 1500;

enum class WifiInitProfile : uint8_t
{
    Lightweight = 0,
    EmergencyLowMemory,
};

struct RuntimeState
{
    bool stack_ready = false;
    bool handlers_registered = false;
    bool wifi_started = false;
    bool wifi_initialized = false;
    bool externally_suspended = false;
    bool ble_paused_for_wifi = false;
    bool network_time_sync_in_progress = false;
    std::time_t network_time_sync_epoch = 0;
    bool config_cached = false;
    bool profiles_cached = false;
    bool connected = false;
    bool connecting = false;
    bool connect_deferred_for_resources = false;
    bool intentional_disconnect_pending = false;
    bool profile_retry_pending = false;
    bool automatic_profile_attempt_active = false;
    bool automatic_profile_shutdown_pending = false;
    bool scanning = false;
    int rssi = -127;
    Config config{};
    Config profiles[kWifiProfileCapacity] = {};
    wifi_config_t station_config{};
    wifi_ap_record_t auto_scan_records[kWifiAutoScanMaxRecords] = {};
    std::size_t profile_count = 0;
    std::size_t next_profile_index = 0;
    std::size_t automatic_profile_failures = 0;
    char ssid[kMaxSsidLength + 1] = {};
    char ip[kMaxIpLength + 1] = {};
    char message[kMaxStatusMessageLength + 1] = {};
    esp_netif_t* sta_netif = nullptr;
    esp_event_handler_instance_t wifi_event_handler = nullptr;
    esp_event_handler_instance_t ip_event_handler = nullptr;
    esp_timer_handle_t profile_retry_timer = nullptr;
    esp_timer_handle_t network_time_sync_timeout_timer = nullptr;
    TaskHandle_t network_time_apply_task = nullptr;
    void* reconnect_memory_reserve = nullptr;
};

RuntimeState s_runtime{};

std::size_t internal_free_bytes()
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

std::size_t internal_largest_block_bytes()
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

std::size_t psram_free_bytes()
{
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

std::size_t psram_largest_block_bytes()
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

void log_heap_snapshot(const char* stage)
{
    const auto snapshot = ::platform::esp::common::memory::capture();
    std::printf("[WiFi][MEM] stage=%s ram_free=%u ram_largest=%u dma_free=%u dma_largest=%u "
                "psram_free=%u psram_largest=%u min_ram=%u min_dma=%u min_psram=%u\n",
                stage ? stage : "state",
                static_cast<unsigned>(snapshot.internal_free),
                static_cast<unsigned>(snapshot.internal_largest),
                static_cast<unsigned>(snapshot.dma_free),
                static_cast<unsigned>(snapshot.dma_largest),
                static_cast<unsigned>(snapshot.psram_free),
                static_cast<unsigned>(snapshot.psram_largest),
                static_cast<unsigned>(snapshot.minimum_internal_free),
                static_cast<unsigned>(snapshot.minimum_dma_free),
                static_cast<unsigned>(snapshot.minimum_psram_free));
}

void set_status_message(const char* message);
void cancel_network_time_sync();

bool radio_is_reserved()
{
    const ::platform::ui::wifi_access::RuntimeStatus access =
        ::platform::ui::wifi_access::status();
    if (!access.non_preemptible_active)
    {
        return false;
    }

    set_status_message("Wireless radio is busy");
    std::printf("[WiFi] start rejected: radio reserved activity=%s\n",
                access.non_preemptible_reason ? access.non_preemptible_reason : "unknown");
    return true;
}

void cancel_profile_retry()
{
    if (s_runtime.profile_retry_timer != nullptr)
    {
        (void)esp_timer_stop(s_runtime.profile_retry_timer);
    }
    s_runtime.profile_retry_pending = false;
}

void reset_automatic_profile_failure_cycle()
{
    s_runtime.automatic_profile_attempt_active = false;
    s_runtime.automatic_profile_shutdown_pending = false;
    s_runtime.automatic_profile_failures = 0;
}

void disable_wifi_after_profile_exhaustion()
{
    s_runtime.automatic_profile_shutdown_pending = false;
    s_runtime.automatic_profile_attempt_active = false;
    s_runtime.automatic_profile_failures = 0;

    Config disabled_config = s_runtime.config;
    disabled_config.enabled = false;
    const bool persisted = save_config(disabled_config);
    if (!persisted)
    {
        // Do not leave the radio retrying just because its disabled state
        // could not be written. The next explicit enable will start a new
        // profile cycle; a reboot can retry only if persistence also failed.
        s_runtime.config = disabled_config;
        s_runtime.config_cached = true;
    }
    const bool stopped = apply_enabled(false);
    std::printf("[WiFi] saved profiles exhausted action=wifi_off persisted=%u stopped=%u\n",
                persisted ? 1U : 0U,
                stopped ? 1U : 0U);
}

void profile_retry_timer_cb(void*)
{
    s_runtime.profile_retry_pending = false;
    if (s_runtime.automatic_profile_shutdown_pending)
    {
        disable_wifi_after_profile_exhaustion();
        return;
    }
    if (!s_runtime.wifi_started || !s_runtime.config_cached || !s_runtime.config.enabled ||
        s_runtime.connected || s_runtime.connecting || s_runtime.profile_count == 0 ||
        radio_is_reserved())
    {
        return;
    }

    std::printf("[WiFi] retrying saved profile=%u/%u\n",
                static_cast<unsigned>(s_runtime.next_profile_index + 1U),
                static_cast<unsigned>(s_runtime.profile_count));
    (void)connect(nullptr);
}

bool ensure_profile_retry_timer()
{
    if (s_runtime.profile_retry_timer != nullptr)
    {
        return true;
    }

    esp_timer_create_args_t args{};
    args.callback = &profile_retry_timer_cb;
    args.name = "wifi_retry";
    const esp_err_t err = esp_timer_create(&args, &s_runtime.profile_retry_timer);
    if (err != ESP_OK)
    {
        std::printf("[WiFi] retry timer create failed err=0x%x\n",
                    static_cast<unsigned>(err));
        return false;
    }
    return true;
}

void schedule_profile_retry()
{
    if (s_runtime.profile_retry_pending || !s_runtime.wifi_started ||
        !s_runtime.config_cached || !s_runtime.config.enabled ||
        s_runtime.profile_count == 0 || radio_is_reserved())
    {
        return;
    }
    if (!ensure_profile_retry_timer())
    {
        return;
    }

    const esp_err_t err = esp_timer_start_once(
        s_runtime.profile_retry_timer,
        static_cast<uint64_t>(kWifiProfileRetryDelayMs) * 1000ULL);
    if (err != ESP_OK)
    {
        std::printf("[WiFi] retry timer start failed err=0x%x\n",
                    static_cast<unsigned>(err));
        return;
    }
    s_runtime.profile_retry_pending = true;
}

void note_automatic_profile_failure(std::size_t failed_index, const char* source)
{
    s_runtime.automatic_profile_attempt_active = false;
    if (s_runtime.profile_count == 0)
    {
        return;
    }

    if (s_runtime.automatic_profile_failures < s_runtime.profile_count)
    {
        ++s_runtime.automatic_profile_failures;
    }
    s_runtime.next_profile_index = (failed_index + 1U) % s_runtime.profile_count;

    if (s_runtime.automatic_profile_failures >= s_runtime.profile_count)
    {
        s_runtime.automatic_profile_shutdown_pending = true;
        std::printf("[WiFi] saved profiles failed=%u/%u source=%s action=wifi_off\n",
                    static_cast<unsigned>(s_runtime.automatic_profile_failures),
                    static_cast<unsigned>(s_runtime.profile_count),
                    source ? source : "unknown");
    }
    else
    {
        std::printf("[WiFi] saved profile failed=%u/%u next=%u/%u source=%s\n",
                    static_cast<unsigned>(s_runtime.automatic_profile_failures),
                    static_cast<unsigned>(s_runtime.profile_count),
                    static_cast<unsigned>(s_runtime.next_profile_index + 1U),
                    static_cast<unsigned>(s_runtime.profile_count),
                    source ? source : "unknown");
    }
    schedule_profile_retry();
}

void release_reconnect_memory_reserve()
{
    if (!s_runtime.reconnect_memory_reserve)
    {
        return;
    }
    heap_caps_free(s_runtime.reconnect_memory_reserve);
    s_runtime.reconnect_memory_reserve = nullptr;
    log_heap_snapshot("released reconnect reserve");
}

void ensure_reconnect_memory_reserve()
{
    if (s_runtime.reconnect_memory_reserve)
    {
        return;
    }
    s_runtime.reconnect_memory_reserve =
        heap_caps_malloc(kWifiReconnectReserveBytes,
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    std::printf("[WiFi][MEM] reconnect reserve bytes=%u acquired=%u\n",
                static_cast<unsigned>(kWifiReconnectReserveBytes),
                s_runtime.reconnect_memory_reserve ? 1U : 0U);
}

bool internal_memory_ready_for_wifi_connect(const char* stage)
{
    release_reconnect_memory_reserve();
    const std::size_t ram_free = internal_free_bytes();
    const std::size_t ram_largest = internal_largest_block_bytes();
    if (ram_free >= kWifiConnectMinInternalFreeBytes &&
        ram_largest >= kWifiConnectMinInternalLargestBlockBytes)
    {
        s_runtime.connect_deferred_for_resources = false;
        return true;
    }

    std::printf("[WiFi][MEM] connect deferred stage=%s ram_free=%u ram_largest=%u min_free=%u min_largest=%u\n",
                stage ? stage : "connect",
                static_cast<unsigned>(ram_free),
                static_cast<unsigned>(ram_largest),
                static_cast<unsigned>(kWifiConnectMinInternalFreeBytes),
                static_cast<unsigned>(kWifiConnectMinInternalLargestBlockBytes));
    s_runtime.connect_deferred_for_resources = true;
    set_status_message("Wi-Fi waiting for memory");
    return false;
}

const char* wifi_profile_name(WifiInitProfile profile)
{
    switch (profile)
    {
    case WifiInitProfile::Lightweight:
        return "lightweight";
    case WifiInitProfile::EmergencyLowMemory:
        return "emergency";
    }
    return "unknown";
}

void apply_wifi_init_profile(wifi_init_config_t& config, WifiInitProfile profile)
{
    config.tx_buf_type = kWifiTxBufferTypeDynamic;
    config.static_tx_buf_num = 0;

    switch (profile)
    {
    case WifiInitProfile::Lightweight:
        config.static_rx_buf_num = kWifiPrimaryStaticRxBufNum;
        config.dynamic_rx_buf_num = kWifiPrimaryDynamicRxBufNum;
        config.dynamic_tx_buf_num = kWifiPrimaryDynamicTxBufNum;
        config.cache_tx_buf_num = std::max(1, kWifiPrimaryCacheTxBufNum);
        config.mgmt_sbuf_num = std::max(6, kWifiPrimaryMgmtSbufNum);
        break;
    case WifiInitProfile::EmergencyLowMemory:
        config.static_rx_buf_num = kWifiRetryStaticRxBufNum;
        config.dynamic_rx_buf_num = kWifiRetryDynamicRxBufNum;
        config.dynamic_tx_buf_num = kWifiRetryDynamicTxBufNum;
        config.cache_tx_buf_num = std::max(1, kWifiRetryCacheTxBufNum);
        config.rx_mgmt_buf_num = std::max(1, kWifiRetryRxMgmtBufNum);
        config.mgmt_sbuf_num = std::max(6, kWifiRetryMgmtSbufNum);
        config.ampdu_rx_enable = 0;
        config.ampdu_tx_enable = 0;
        config.amsdu_tx_enable = 0;
        config.rx_ba_win = 0;
        break;
    }
}

void log_wifi_init_profile(const char* attempt_tag,
                           WifiInitProfile profile,
                           const wifi_init_config_t& config)
{
    std::printf("[WiFi][CFG] attempt=%s profile=%s static_rx=%d dynamic_rx=%d tx_type=%d static_tx=%d dynamic_tx=%d "
                "cache_tx=%d rx_mgmt=%d mgmt_sbuf=%d ampdu_rx=%d ampdu_tx=%d amsdu_tx=%d rx_ba_win=%d\n",
                attempt_tag ? attempt_tag : "attempt",
                wifi_profile_name(profile),
                config.static_rx_buf_num,
                config.dynamic_rx_buf_num,
                config.tx_buf_type,
                config.static_tx_buf_num,
                config.dynamic_tx_buf_num,
                config.cache_tx_buf_num,
                config.rx_mgmt_buf_num,
                config.mgmt_sbuf_num,
                config.ampdu_rx_enable,
                config.ampdu_tx_enable,
                config.amsdu_tx_enable,
                config.rx_ba_win);
}

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const char* source = text ? text : "";
    std::snprintf(out, out_len, "%s", source);
}

void insert_scan_result_sorted(ScanResult* out_results,
                               std::size_t capacity,
                               std::size_t& out_count,
                               const wifi_ap_record_t& record)
{
    if (out_results == nullptr || capacity == 0 || record.ssid[0] == 0)
    {
        return;
    }

    ScanResult result{};
    copy_bounded(result.ssid, sizeof(result.ssid), reinterpret_cast<const char*>(record.ssid));
    result.rssi = record.rssi;
    result.requires_password = record.authmode != WIFI_AUTH_OPEN;

    std::size_t insert_at = out_count;
    if (out_count < capacity)
    {
        ++out_count;
    }
    else if (result.rssi <= out_results[capacity - 1U].rssi)
    {
        return;
    }
    else
    {
        insert_at = capacity - 1U;
    }

    while (insert_at > 0 && out_results[insert_at - 1U].rssi < result.rssi)
    {
        out_results[insert_at] = out_results[insert_at - 1U];
        --insert_at;
    }
    out_results[insert_at] = result;
}

void set_status_message(const char* message)
{
    copy_bounded(s_runtime.message, sizeof(s_runtime.message), message);
}

void cache_config(const Config& config)
{
    s_runtime.config = config;
    s_runtime.config_cached = true;
}

bool has_saved_credentials(const Config& config);

void profile_key(char* out, std::size_t out_len, const char* field, std::size_t index)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "wifi_%s_%u", field ? field : "profile", static_cast<unsigned>(index));
}

bool same_ssid(const Config& lhs, const Config& rhs)
{
    return std::strncmp(lhs.ssid, rhs.ssid, sizeof(lhs.ssid)) == 0;
}

int profile_index_for_ssid(const char* ssid)
{
    if (!ssid || ssid[0] == '\0')
    {
        return -1;
    }
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (std::strncmp(s_runtime.profiles[i].ssid, ssid, sizeof(s_runtime.profiles[i].ssid)) == 0)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void clear_profiles()
{
    for (Config& profile : s_runtime.profiles)
    {
        profile = Config{};
    }
    s_runtime.profile_count = 0;
    s_runtime.profiles_cached = true;
}

void clamp_next_profile_index()
{
    if (s_runtime.profile_count == 0)
    {
        s_runtime.next_profile_index = 0;
        return;
    }
    s_runtime.next_profile_index %= s_runtime.profile_count;
}

void append_profile_unique(const Config& profile)
{
    if (!has_saved_credentials(profile))
    {
        return;
    }
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (same_ssid(s_runtime.profiles[i], profile))
        {
            return;
        }
    }
    if (s_runtime.profile_count >= kWifiProfileCapacity)
    {
        return;
    }
    s_runtime.profiles[s_runtime.profile_count] = profile;
    s_runtime.profiles[s_runtime.profile_count].enabled = true;
    ++s_runtime.profile_count;
}

void upsert_profile_front(const Config& profile)
{
    if (!has_saved_credentials(profile))
    {
        return;
    }

    std::size_t existing = s_runtime.profile_count;
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (same_ssid(s_runtime.profiles[i], profile))
        {
            existing = i;
            break;
        }
    }

    std::size_t last = s_runtime.profile_count;
    if (existing < s_runtime.profile_count)
    {
        last = existing;
    }
    else if (s_runtime.profile_count < kWifiProfileCapacity)
    {
        last = s_runtime.profile_count;
        ++s_runtime.profile_count;
    }
    else
    {
        last = kWifiProfileCapacity - 1U;
    }

    for (std::size_t i = last; i > 0; --i)
    {
        s_runtime.profiles[i] = s_runtime.profiles[i - 1U];
    }
    s_runtime.profiles[0] = profile;
    s_runtime.profiles[0].enabled = true;
    s_runtime.next_profile_index = 0;
    s_runtime.profiles_cached = true;
}

bool persist_profiles()
{
    for (std::size_t i = 0; i < kWifiProfileCapacity; ++i)
    {
        char ssid_key[24] = {};
        char password_key[24] = {};
        profile_key(ssid_key, sizeof(ssid_key), "ssid", i);
        profile_key(password_key, sizeof(password_key), "password", i);
        const Config& profile = s_runtime.profiles[i];
        if (!::platform::ui::settings_store::put_string(
                kSettingsNs,
                ssid_key,
                i < s_runtime.profile_count ? profile.ssid : "") ||
            !::platform::ui::settings_store::put_string(
                kSettingsNs,
                password_key,
                i < s_runtime.profile_count ? profile.password : "")) return false;
    }
    // Publish the count only after all slots were committed. The SD working
    // document remains the recovery authority if a multi-key write is partial.
    ::platform::ui::settings_store::put_int(kSettingsNs, kWifiProfileCountKey,
                                            static_cast<int>(s_runtime.profile_count));
    return ::platform::ui::settings_store::get_int(kSettingsNs, kWifiProfileCountKey, -1) ==
           static_cast<int>(s_runtime.profile_count);
}

bool read_profile_from_store(std::size_t index, Config& profile)
{
    profile = Config{};
    profile.enabled = true;

    char ssid_key[24] = {};
    char password_key[24] = {};
    profile_key(ssid_key, sizeof(ssid_key), "ssid", index);
    profile_key(password_key, sizeof(password_key), "password", index);

    std::string value;
    if (!::platform::ui::settings_store::get_string(kSettingsNs, ssid_key, value))
    {
        return false;
    }
    copy_bounded(profile.ssid, sizeof(profile.ssid), value.c_str());
    if (!has_saved_credentials(profile))
    {
        return false;
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, password_key, value))
    {
        copy_bounded(profile.password, sizeof(profile.password), value.c_str());
    }
    else
    {
        char legacy_password_key[24] = {};
        profile_key(legacy_password_key, sizeof(legacy_password_key), "pass", index);
        value.clear();
        if (::platform::ui::settings_store::get_string(kSettingsNs, legacy_password_key, value))
        {
            copy_bounded(profile.password, sizeof(profile.password), value.c_str());
            const bool migrated = ::platform::ui::settings_store::put_string(
                kSettingsNs,
                password_key,
                profile.password);
            const char* legacy_keys[] = {legacy_password_key};
            if (migrated) ::platform::ui::settings_store::remove_keys(kSettingsNs, legacy_keys, 1);
        }
    }

    return true;
}

bool read_config_from_store(Config& out)
{
    out = Config{};
    out.enabled = ::platform::ui::settings_store::get_bool(kSettingsNs, kWifiEnabledKey, false);

    std::string value;
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiSsidKey, value))
    {
        copy_bounded(out.ssid, sizeof(out.ssid), value.c_str());
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiPasswordKey, value))
    {
        copy_bounded(out.password, sizeof(out.password), value.c_str());
    }
    const bool has_primary_credentials = has_saved_credentials(out);

    const std::size_t next_profile_index = s_runtime.next_profile_index;
    clear_profiles();
    s_runtime.next_profile_index = next_profile_index;
    const int stored_count = std::clamp(
        ::platform::ui::settings_store::get_int(kSettingsNs, kWifiProfileCountKey, 0),
        0,
        static_cast<int>(kWifiProfileCapacity));
    const int profile_read_count =
        stored_count > 0 ? stored_count : static_cast<int>(kWifiProfileCapacity);
    std::size_t loaded_profile_count = 0;
    for (int i = 0; i < profile_read_count; ++i)
    {
        Config profile{};
        if (read_profile_from_store(static_cast<std::size_t>(i), profile))
        {
            append_profile_unique(profile);
            ++loaded_profile_count;
        }
    }
    append_profile_unique(out);
    if (stored_count == 0 && loaded_profile_count > 0)
    {
        std::printf("[WiFi] recovered saved profile count=%u from profile keys\n",
                    static_cast<unsigned>(s_runtime.profile_count));
        (void)persist_profiles();
    }
    clamp_next_profile_index();
    if (!has_primary_credentials && s_runtime.profile_count > 0)
    {
        copy_bounded(out.ssid, sizeof(out.ssid), s_runtime.profiles[0].ssid);
        copy_bounded(out.password, sizeof(out.password), s_runtime.profiles[0].password);
    }

    cache_config(out);
    return true;
}

const Config& current_config()
{
    if (!s_runtime.config_cached)
    {
        Config config{};
        (void)read_config_from_store(config);
    }
    return s_runtime.config;
}

bool has_saved_credentials(const Config& config)
{
    return config.ssid[0] != '\0';
}

ConnectionState disconnected_state(bool enabled, bool has_credentials)
{
    if (!enabled)
    {
        return ConnectionState::Disabled;
    }
    return has_credentials ? ConnectionState::Idle : ConnectionState::Error;
}

void clear_connection_details()
{
    cancel_network_time_sync();
    s_runtime.connected = false;
    s_runtime.connecting = false;
    s_runtime.rssi = -127;
    s_runtime.ssid[0] = '\0';
    s_runtime.ip[0] = '\0';
}

void refresh_runtime_status_message()
{
    const Config& config = s_runtime.config;

    if (!s_runtime.config_cached || !config.enabled)
    {
        set_status_message("Wi-Fi disabled");
        return;
    }

    if (s_runtime.scanning)
    {
        set_status_message("Scanning...");
        return;
    }

    if (s_runtime.connecting)
    {
        set_status_message("Connecting...");
        return;
    }

    if (s_runtime.connected)
    {
        if (s_runtime.ip[0] != '\0')
        {
            char buffer[kMaxStatusMessageLength + 1];
            std::snprintf(buffer,
                          sizeof(buffer),
                          "Connected %s",
                          s_runtime.ip);
            set_status_message(buffer);
        }
        else
        {
            set_status_message("Connected");
        }
        return;
    }

    if (!has_saved_credentials(config))
    {
        set_status_message("Set SSID and password");
        return;
    }

    set_status_message("Ready to connect");
}

bool runtime_ble_is_enabled()
{
#if TRAIL_MATE_ENABLE_BLE
    if (!app::hasAppFacade())
    {
        return false;
    }

#if TRAIL_MATE_WIFI_HAS_BLE_MANAGER
    if (ble::BleManager* ble_manager = app::appFacade().getBleManager())
    {
        return ble_manager->isEnabled();
    }
#endif

    return app::appFacade().isBleEnabled();
#else
    return false;
#endif
}

bool is_valid_network_epoch(std::time_t epoch_seconds)
{
    return epoch_seconds >= kNetworkTimeMinValidEpochSeconds;
}

void stop_sntp_once()
{
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }
    esp_sntp_set_time_sync_notification_cb(nullptr);
}

// A time sync is strictly bound to the current IP lease. Keep the timer
// allocated with the Wi-Fi runtime so repeated DHCP events do not fragment
// scarce internal memory, but make every terminal path stop it and unregister
// the SNTP callback before another lease can start a new sync.
void finish_network_time_sync()
{
    s_runtime.network_time_sync_in_progress = false;
    if (s_runtime.network_time_sync_timeout_timer != nullptr)
    {
        (void)esp_timer_stop(s_runtime.network_time_sync_timeout_timer);
    }
    stop_sntp_once();
}

void cancel_network_time_sync()
{
    finish_network_time_sync();
}

void network_time_apply_task(void*)
{
    const std::time_t epoch_seconds = s_runtime.network_time_sync_epoch;

    if (is_valid_network_epoch(epoch_seconds))
    {
        const bool applied =
            ::platform::esp::boards::applySystemTimeAndSyncBoardRtc(epoch_seconds, "wifi_sntp");
        std::printf("[WiFi][Time] SNTP synced epoch=%lld rtc=%s\n",
                    static_cast<long long>(epoch_seconds),
                    applied ? "updated" : "update_failed");
    }
    else
    {
        std::printf("[WiFi][Time] SNTP completed with invalid epoch=%lld\n",
                    static_cast<long long>(epoch_seconds));
    }

    finish_network_time_sync();
    s_runtime.network_time_apply_task = nullptr;
    vTaskDelete(nullptr);
}

void network_time_sync_notification_cb(timeval* tv)
{
    if (!s_runtime.network_time_sync_in_progress)
    {
        return;
    }

    if (s_runtime.network_time_sync_timeout_timer != nullptr)
    {
        (void)esp_timer_stop(s_runtime.network_time_sync_timeout_timer);
    }
    s_runtime.network_time_sync_epoch = tv != nullptr ? tv->tv_sec : std::time(nullptr);

    if (s_runtime.network_time_apply_task != nullptr)
    {
        return;
    }

    const BaseType_t created = xTaskCreate(network_time_apply_task,
                                           "wifi_time_apply",
                                           kNetworkTimeApplyTaskStackBytes,
                                           nullptr,
                                           tskIDLE_PRIORITY + 1,
                                           &s_runtime.network_time_apply_task);
    if (created != pdPASS)
    {
        finish_network_time_sync();
        s_runtime.network_time_apply_task = nullptr;
        std::printf("[WiFi][Time] failed to start SNTP apply task\n");
    }
}

void network_time_sync_timeout_cb(void*)
{
    if (!s_runtime.network_time_sync_in_progress)
    {
        return;
    }

    std::printf("[WiFi][Time] SNTP sync timed out server=%s\n", kNetworkTimeSyncServer);
    finish_network_time_sync();
}

bool ensure_network_time_sync_timer()
{
    if (s_runtime.network_time_sync_timeout_timer != nullptr)
    {
        return true;
    }

    esp_timer_create_args_t args{};
    args.callback = &network_time_sync_timeout_cb;
    args.name = "wifi_time";
    const esp_err_t err = esp_timer_create(&args, &s_runtime.network_time_sync_timeout_timer);
    if (err != ESP_OK)
    {
        std::printf("[WiFi][Time] failed to create SNTP timeout timer err=0x%x\n",
                    static_cast<unsigned>(err));
        return false;
    }
    return true;
}

void start_or_restart_sntp()
{
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(&network_time_sync_notification_cb);
    esp_sntp_setservername(0, kNetworkTimeSyncServer);

    if (!esp_sntp_enabled())
    {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
        esp_sntp_init();
        std::printf("[WiFi][Time] SNTP started server=%s\n", kNetworkTimeSyncServer);
        return;
    }

    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    if (esp_sntp_restart())
    {
        std::printf("[WiFi][Time] SNTP restarted server=%s\n", kNetworkTimeSyncServer);
    }
}

void request_network_time_sync()
{
    if (s_runtime.network_time_sync_in_progress)
    {
        std::printf("[WiFi][Time] duplicate GOT_IP ignored while SNTP is in progress\n");
        return;
    }

    if (!ensure_network_time_sync_timer())
    {
        return;
    }

    s_runtime.network_time_sync_in_progress = true;
    const esp_err_t timer_err =
        esp_timer_start_once(s_runtime.network_time_sync_timeout_timer,
                             static_cast<uint64_t>(kNetworkTimeSyncTimeoutMs) * 1000ULL);
    if (timer_err != ESP_OK)
    {
        finish_network_time_sync();
        std::printf("[WiFi][Time] failed to start SNTP timeout timer err=0x%x\n",
                    static_cast<unsigned>(timer_err));
        return;
    }

    start_or_restart_sntp();
}

bool pause_runtime_ble_for_wifi()
{
#if TRAIL_MATE_ENABLE_BLE
    if (s_runtime.ble_paused_for_wifi || !app::hasAppFacade())
    {
        return false;
    }

#if TRAIL_MATE_WIFI_HAS_BLE_MANAGER
    if (ble::BleManager* ble_manager = app::appFacade().getBleManager())
    {
        if (!ble_manager->isEnabled())
        {
            return false;
        }
        std::printf("[WiFi][BLE] pausing BLE to free internal RAM for Wi-Fi init retry\n");
        ble_manager->setEnabled(false);
        s_runtime.ble_paused_for_wifi = true;
        return true;
    }
#endif

    if (!app::appFacade().isBleEnabled())
    {
        return false;
    }

    std::printf("[WiFi][BLE] pausing BLE runtime facade for Wi-Fi init retry\n");
    app::appFacade().setBleEnabled(false);
    s_runtime.ble_paused_for_wifi = true;
    return true;
#else
    s_runtime.ble_paused_for_wifi = false;
    return false;
#endif
}

void restore_runtime_ble_after_wifi(const char* reason)
{
#if TRAIL_MATE_ENABLE_BLE
    if (!s_runtime.ble_paused_for_wifi || !app::hasAppFacade())
    {
        return;
    }

#if TRAIL_MATE_WIFI_HAS_BLE_MANAGER
    if (ble::BleManager* ble_manager = app::appFacade().getBleManager())
    {
        std::printf("[WiFi][BLE] restoring BLE (%s)\n", reason ? reason : "after Wi-Fi");
        ble_manager->setEnabled(true);
        s_runtime.ble_paused_for_wifi = false;
        return;
    }
#endif

    std::printf("[WiFi][BLE] restoring BLE runtime facade (%s)\n", reason ? reason : "after Wi-Fi");
    app::appFacade().setBleEnabled(true);
    s_runtime.ble_paused_for_wifi = false;
#else
    (void)reason;
    s_runtime.ble_paused_for_wifi = false;
#endif
}

bool ensure_stack_ready()
{
    if (s_runtime.stack_ready)
    {
        return true;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    trail_mate_tab5_set_wifi_power_enabled(true);
#endif

    const esp_err_t netif_err = esp_netif_init();
    if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE)
    {
        set_status_message("esp_netif_init failed");
        return false;
    }

    const esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
    {
        set_status_message("event loop init failed");
        return false;
    }

    if (s_runtime.sta_netif == nullptr)
    {
        // Team ESP-NOW pairing may have initialized the globally-owned default
        // STA netif first. Adopt it instead of relying only on this runtime's
        // private pointer and triggering ESP-IDF's duplicate-key assertion.
        s_runtime.sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (s_runtime.sta_netif == nullptr)
        {
            s_runtime.sta_netif = esp_netif_create_default_wifi_sta();
        }
        if (s_runtime.sta_netif == nullptr)
        {
            set_status_message("STA netif create failed");
            return false;
        }
    }

    s_runtime.stack_ready = true;
    return true;
}

void wifi_event_handler(void*,
                        esp_event_base_t event_base,
                        int32_t event_id,
                        void* event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_CONNECTED:
        {
            const auto* connected = static_cast<wifi_event_sta_connected_t*>(event_data);
            char ssid[kMaxSsidLength + 1]{};
            if (connected)
            {
                const std::size_t ssid_len =
                    std::min<std::size_t>(connected->ssid_len, sizeof(ssid) - 1);
                std::memcpy(ssid, connected->ssid, ssid_len);
                ssid[ssid_len] = '\0';
                std::printf("[WiFi] sta connected ssid=%s channel=%u auth=%u\n",
                            ssid[0] ? ssid : "<hidden>",
                            static_cast<unsigned>(connected->channel),
                            static_cast<unsigned>(connected->authmode));
            }
            else
            {
                std::printf("[WiFi] sta connected\n");
            }
            // Association is only an intermediate state. Keep the attempt
            // active until DHCP produces GOT_IP or a disconnect event
            // terminates it.
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            const auto* disconnected = static_cast<wifi_event_sta_disconnected_t*>(event_data);
            char ssid[kMaxSsidLength + 1]{};
            if (disconnected)
            {
                const std::size_t ssid_len =
                    std::min<std::size_t>(disconnected->ssid_len, sizeof(ssid) - 1);
                std::memcpy(ssid, disconnected->ssid, ssid_len);
                ssid[ssid_len] = '\0';
                std::printf("[WiFi] sta disconnected reason=%u ssid=%s\n",
                            static_cast<unsigned>(disconnected->reason),
                            ssid[0] ? ssid : current_config().ssid);
            }
            else
            {
                std::printf("[WiFi] sta disconnected ssid=%s\n",
                            current_config().ssid[0] ? current_config().ssid : "<unset>");
            }
            if (s_runtime.externally_suspended)
            {
                // USB Disk owns the radio teardown. A driver disconnect is
                // expected here and must never consume a saved-profile retry
                // or persist the user's enabled preference as false.
                s_runtime.intentional_disconnect_pending = false;
                clear_connection_details();
                refresh_runtime_status_message();
                std::printf("[WiFi] USB Disk suspension disconnect ignored\n");
                break;
            }
            if (s_runtime.intentional_disconnect_pending)
            {
                s_runtime.intentional_disconnect_pending = false;
                std::printf("[WiFi] intentional disconnect completed\n");
                break;
            }

            const bool retry_after_disconnect = s_runtime.connecting || s_runtime.connected;
            const bool failed_automatic_profile =
                s_runtime.connecting && s_runtime.automatic_profile_attempt_active;
            const std::size_t failed_profile_index = s_runtime.next_profile_index;
            clear_connection_details();
            refresh_runtime_status_message();
            if (failed_automatic_profile)
            {
                // Only an association failure may advance the saved-profile
                // cycle. A service, DNS, or reachability failure while this
                // station is already connected never reaches this path.
                note_automatic_profile_failure(failed_profile_index, "association");
            }
            else if (retry_after_disconnect && s_runtime.profile_count > 0)
            {
                // A station that had reached GOT_IP retries its current SSID
                // first. Do not silently turn a connected-session loss into a
                // profile switch.
                std::printf("[WiFi] station link lost action=retry_current_profile index=%u/%u\n",
                            static_cast<unsigned>(s_runtime.next_profile_index + 1U),
                            static_cast<unsigned>(s_runtime.profile_count));
                schedule_profile_retry();
            }
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            s_runtime.scanning = false;
            refresh_runtime_status_message();
            break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        if (s_runtime.externally_suspended)
        {
            std::printf("[WiFi] USB Disk suspension GOT_IP ignored\n");
            return;
        }
        const auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
        s_runtime.connected = true;
        s_runtime.connecting = false;
        reset_automatic_profile_failure_cycle();
        if (got_ip)
        {
            std::snprintf(s_runtime.ip,
                          sizeof(s_runtime.ip),
                          IPSTR,
                          IP2STR(&got_ip->ip_info.ip));
        }
        wifi_ap_record_t ap_info{};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
            s_runtime.rssi = ap_info.rssi;
            copy_bounded(s_runtime.ssid,
                         sizeof(s_runtime.ssid),
                         reinterpret_cast<const char*>(ap_info.ssid));
        }
        refresh_runtime_status_message();
        std::printf("[WiFi] got ip=%s ssid=%s rssi=%d\n",
                    s_runtime.ip[0] ? s_runtime.ip : "<none>",
                    s_runtime.ssid[0] ? s_runtime.ssid : current_config().ssid,
                    s_runtime.rssi);
        std::printf("[WiFi][Time] GOT_IP triggers one-shot SNTP\n");
        request_network_time_sync();
        ensure_reconnect_memory_reserve();
    }
}

bool initialize_wifi_driver_once(const char* attempt_tag, WifiInitProfile profile)
{
    char stage[48];
    std::snprintf(stage, sizeof(stage), "before init %s", attempt_tag ? attempt_tag : "attempt");
    log_heap_snapshot(stage);

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    apply_wifi_init_profile(config, profile);
    log_wifi_init_profile(attempt_tag, profile, config);
    const esp_err_t init_err = esp_wifi_init(&config);
    if (init_err != ESP_OK && init_err != ESP_ERR_WIFI_INIT_STATE)
    {
        std::printf("[WiFi] esp_wifi_init failed attempt=%s err=0x%x\n",
                    attempt_tag ? attempt_tag : "attempt",
                    static_cast<unsigned>(init_err));
        std::snprintf(stage, sizeof(stage), "after init fail %s", attempt_tag ? attempt_tag : "attempt");
        log_heap_snapshot(stage);
        return false;
    }

    const esp_err_t storage_err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (storage_err != ESP_OK)
    {
        std::printf("[WiFi] esp_wifi_set_storage failed attempt=%s err=0x%x\n",
                    attempt_tag ? attempt_tag : "attempt",
                    static_cast<unsigned>(storage_err));
        (void)esp_wifi_deinit();
        set_status_message("wifi storage failed");
        return false;
    }

    const esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (mode_err != ESP_OK)
    {
        std::printf("[WiFi] esp_wifi_set_mode failed attempt=%s err=0x%x\n",
                    attempt_tag ? attempt_tag : "attempt",
                    static_cast<unsigned>(mode_err));
        (void)esp_wifi_deinit();
        set_status_message("wifi mode failed");
        return false;
    }

    const esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (ps_err != ESP_OK)
    {
        std::printf("[WiFi] esp_wifi_set_ps failed attempt=%s err=0x%x\n",
                    attempt_tag ? attempt_tag : "attempt",
                    static_cast<unsigned>(ps_err));
        (void)esp_wifi_deinit();
        set_status_message("wifi power-save failed");
        return false;
    }

    std::snprintf(stage, sizeof(stage), "after init %s", attempt_tag ? attempt_tag : "attempt");
    log_heap_snapshot(stage);
    return true;
}

bool ensure_wifi_initialized()
{
    if (s_runtime.wifi_initialized)
    {
        return true;
    }

    if (!ensure_stack_ready())
    {
        return false;
    }

    if (!initialize_wifi_driver_once("primary", WifiInitProfile::Lightweight))
    {
        if (runtime_ble_is_enabled() && pause_runtime_ble_for_wifi())
        {
            vTaskDelay(pdMS_TO_TICKS(kBleRetryDelayMs));
            log_heap_snapshot("after BLE pause");
            (void)esp_wifi_deinit();

            if (!initialize_wifi_driver_once("retry", WifiInitProfile::EmergencyLowMemory))
            {
                restore_runtime_ble_after_wifi("Wi-Fi init retry failed");
                set_status_message("esp_wifi_init failed");
                return false;
            }

            std::printf("[WiFi][BLE] Wi-Fi init retry succeeded with BLE paused for this Wi-Fi session\n");
        }
        else
        {
            set_status_message("esp_wifi_init failed");
            return false;
        }
    }

    if (!s_runtime.handlers_registered)
    {
        if (esp_event_handler_instance_register(
                WIFI_EVENT,
                ESP_EVENT_ANY_ID,
                &wifi_event_handler,
                nullptr,
                &s_runtime.wifi_event_handler) != ESP_OK)
        {
            set_status_message("wifi event hook failed");
            return false;
        }

        if (esp_event_handler_instance_register(
                IP_EVENT,
                IP_EVENT_STA_GOT_IP,
                &wifi_event_handler,
                nullptr,
                &s_runtime.ip_event_handler) != ESP_OK)
        {
            set_status_message("ip event hook failed");
            return false;
        }

        s_runtime.handlers_registered = true;
    }

    s_runtime.wifi_initialized = true;
    return true;
}

bool ensure_wifi_started()
{
    if (s_runtime.externally_suspended)
    {
        set_status_message("Wi-Fi suspended for USB Disk");
        return false;
    }
    if (s_runtime.wifi_started)
    {
        return true;
    }

    if (!ensure_wifi_initialized())
    {
        return false;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    trail_mate_tab5_set_wifi_power_enabled(true);
#endif

    const esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN)
    {
        std::printf("[WiFi] esp_wifi_start failed err=0x%x\n",
                    static_cast<unsigned>(start_err));
        if (s_runtime.ble_paused_for_wifi)
        {
            (void)esp_wifi_deinit();
            s_runtime.wifi_initialized = false;
            restore_runtime_ble_after_wifi("Wi-Fi start failed");
        }
        set_status_message("wifi start failed");
        return false;
    }

    s_runtime.wifi_started = true;
    refresh_runtime_status_message();
    return true;
}

bool stop_wifi_runtime(const char* success_message)
{
    reset_automatic_profile_failure_cycle();
    cancel_profile_retry();
    if (!::platform::ui::wifi_access::set_transport_enabled(false))
    {
        std::printf("[WiFi] transport clients failed to quiesce; keeping driver active\n");
        set_status_message("Wi-Fi clients busy");
        return false;
    }
    if (s_runtime.wifi_started)
    {
        (void)esp_wifi_disconnect();
        const auto stopped = esp_wifi_stop();
        if (stopped != ESP_OK && stopped != ESP_ERR_WIFI_NOT_INIT)
        {
            set_status_message("Wi-Fi driver could not stop");
            return false;
        }
        s_runtime.wifi_started = false;
        clear_connection_details();
    }
    if (s_runtime.wifi_initialized)
    {
        const esp_err_t deinit_err = esp_wifi_deinit();
        if (deinit_err != ESP_OK && deinit_err != ESP_ERR_WIFI_NOT_INIT)
        {
            std::printf("[WiFi] esp_wifi_deinit failed err=0x%x\n",
                        static_cast<unsigned>(deinit_err));
            // Keep the initialization flag and reserve truthful so a retry
            // can finish cleanup instead of allocating a second Wi-Fi owner.
            set_status_message("Wi-Fi driver cleanup failed");
            return false;
        }
        else
        {
            log_heap_snapshot("after deinit");
        }
    }
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    trail_mate_tab5_set_wifi_power_enabled(false);
#endif
    s_runtime.wifi_started = false;
    s_runtime.wifi_initialized = false;
    release_reconnect_memory_reserve();
    clear_connection_details();
    restore_runtime_ble_after_wifi(success_message);
    set_status_message(success_message);
    return true;
}

} // namespace

bool is_supported()
{
    return true;
}

bool load_config(Config& out)
{
    return read_config_from_store(out);
}

bool save_config(const Config& config)
{
    ::platform::ui::settings_store::ScopedChangeBatch changes;
    if (!s_runtime.profiles_cached)
    {
        Config ignored{};
        (void)read_config_from_store(ignored);
    }
    upsert_profile_front(config);
    const bool profiles_ok = persist_profiles();
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, config.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, config.enabled);
    const bool enabled_ok = ::platform::ui::settings_store::get_bool(
                                kSettingsNs, kWifiEnabledKey, !config.enabled) == config.enabled;
    if (!profiles_ok || !ssid_ok || !password_ok || !enabled_ok)
    {
        s_runtime.profiles_cached = false;
        s_runtime.config_cached = false;
        return false;
    }
    cache_config(config);
    refresh_runtime_status_message();
    return true;
}

bool find_saved_config(const char* ssid, Config& out)
{
    out = Config{};
    if (!ssid || ssid[0] == '\0')
    {
        return false;
    }
    if (!s_runtime.profiles_cached)
    {
        Config ignored{};
        (void)read_config_from_store(ignored);
    }
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (std::strncmp(s_runtime.profiles[i].ssid, ssid, sizeof(s_runtime.profiles[i].ssid)) == 0)
        {
            out = s_runtime.profiles[i];
            out.enabled = s_runtime.config_cached ? s_runtime.config.enabled : out.enabled;
            return true;
        }
    }
    return false;
}

bool visit_saved_profiles(bool* enabled,
                          std::size_t* count,
                          SavedProfileVisitor visitor,
                          void* context)
{
    if (!enabled || !count || !visitor)
    {
        return false;
    }
    if (!s_runtime.profiles_cached)
    {
        Config ignored{};
        (void)read_config_from_store(ignored);
    }
    *enabled = s_runtime.config_cached ? s_runtime.config.enabled : false;
    *count = s_runtime.profile_count;
    for (std::size_t index = 0U; index < s_runtime.profile_count; ++index)
    {
        if (!visitor(context, index, s_runtime.profiles[index]))
        {
            return false;
        }
    }
    return true;
}

bool read_saved_profiles_into(bool* enabled, std::size_t* count,
                              Config* profiles, std::size_t capacity)
{
    return ::platform::esp::common::read_persisted_wifi_profiles(enabled, count, profiles, capacity);
}

bool replace_saved_profiles(bool enabled, const Config* profiles, std::size_t count)
{
    ::platform::ui::settings_store::ScopedChangeBatch changes;
    if (count > kWifiProfileCapacity || (count != 0U && !profiles))
    {
        return false;
    }
    for (std::size_t index = 0U; index < count; ++index)
    {
        if (!std::memchr(profiles[index].ssid, '\0', sizeof(profiles[index].ssid)) ||
            !std::memchr(profiles[index].password, '\0', sizeof(profiles[index].password)) ||
            !has_saved_credentials(profiles[index]))
        {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous)
        {
            if (same_ssid(profiles[index], profiles[previous]))
            {
                return false;
            }
        }
    }

    clear_profiles();
    for (std::size_t index = 0U; index < count; ++index)
    {
        s_runtime.profiles[index] = profiles[index];
        s_runtime.profiles[index].enabled = true;
    }
    s_runtime.profile_count = count;
    s_runtime.next_profile_index = 0U;
    s_runtime.profiles_cached = true;

    Config primary{};
    primary.enabled = enabled;
    if (count > 0U)
    {
        primary = s_runtime.profiles[0];
        primary.enabled = enabled;
    }
    const bool profiles_ok = persist_profiles();
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, primary.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, primary.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, enabled);
    const bool enabled_ok = ::platform::ui::settings_store::get_bool(
                                kSettingsNs, kWifiEnabledKey, !enabled) == enabled;
    if (!profiles_ok || !ssid_ok || !password_ok || !enabled_ok)
    {
        s_runtime.profiles_cached = false;
        s_runtime.config_cached = false;
        return false;
    }
    cache_config(primary);
    refresh_runtime_status_message();
    return true;
}

bool apply_enabled(bool enabled)
{
    if (!is_supported())
    {
        return false;
    }

    if (enabled && radio_is_reserved())
    {
        return false;
    }

    if (!enabled)
    {
        return stop_wifi_runtime("Wi-Fi disabled");
    }

    if (!ensure_wifi_started())
    {
        return false;
    }
    // An explicit user enable starts a fresh, bounded saved-profile cycle.
    reset_automatic_profile_failure_cycle();
    if (!::platform::ui::wifi_access::set_transport_enabled(true))
    {
        set_status_message("Wi-Fi clients unavailable");
        return false;
    }

    clear_connection_details();
    refresh_runtime_status_message();
    Config config{};
    if (load_config(config) && config.enabled && has_saved_credentials(config))
    {
        // Enabling Wi-Fi is expected to resume the saved-profile cycle even
        // when no MQTT, Reticulum, or other network client is active yet.
        (void)connect(nullptr);
    }
    return true;
}

bool set_user_enabled(bool enabled)
{
    const auto access = ::platform::ui::wifi_access::status();
    if (!is_supported() || s_runtime.externally_suspended || access.non_preemptible_active ||
        access.ota_active || access.exclusive_owner != ::platform::ui::wifi_access::ExclusiveOwner::None)
        return false;
    Config previous{};
    if (!load_config(previous)) return false;
    const auto persist = [](bool value)
    {
        ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, value);
        if (::platform::ui::settings_store::get_bool(kSettingsNs, kWifiEnabledKey, !value) != value) return false;
        s_runtime.config.enabled = value;
        s_runtime.config_cached = true;
        return true;
    };
    if (!persist(enabled)) return false;
    if (apply_enabled(enabled)) return true;
    (void)persist(previous.enabled);
    (void)apply_enabled(previous.enabled);
    return false;
}

bool suspend_for_external_storage(ExternalStorageSuspension* out_suspension)
{
    if (!out_suspension)
    {
        return false;
    }
    *out_suspension = ExternalStorageSuspension{};
    if (s_runtime.externally_suspended)
    {
        return false;
    }

    const bool active = s_runtime.wifi_started || s_runtime.wifi_initialized;
    s_runtime.externally_suspended = true;
    if (!active)
    {
        return true;
    }
    if (!stop_wifi_runtime("Wi-Fi suspended for USB Disk"))
    {
        s_runtime.externally_suspended = false;
        return false;
    }
    out_suspension->resume_required = true;
    return true;
}

void resume_after_external_storage(ExternalStorageSuspension* suspension)
{
    if (!suspension)
    {
        return;
    }
    const bool resume_required = suspension->resume_required;
    *suspension = ExternalStorageSuspension{};
    s_runtime.externally_suspended = false;
    if (!resume_required)
    {
        return;
    }

    const bool resumed = apply_enabled(true);
    std::printf("[WiFi] usb storage resume requested=%u resumed=%u\n",
                resume_required ? 1U : 0U,
                resumed ? 1U : 0U);
}

enum class ConnectStartResult : uint8_t
{
    Started,
    DeferredForResources,
    NotAttempted,
    Failed,
};

static ConnectStartResult connect_single_profile(const Config& config)
{
    if (!config.enabled)
    {
        set_status_message("Enable Wi-Fi first");
        return ConnectStartResult::NotAttempted;
    }

    if (!has_saved_credentials(config))
    {
        set_status_message("SSID is not set");
        return ConnectStartResult::NotAttempted;
    }

    if (radio_is_reserved())
    {
        return ConnectStartResult::NotAttempted;
    }

    if (!ensure_wifi_started())
    {
        return ConnectStartResult::NotAttempted;
    }

    if (!internal_memory_ready_for_wifi_connect("before esp_wifi_connect"))
    {
        return ConnectStartResult::DeferredForResources;
    }

    s_runtime.station_config = wifi_config_t{};
    copy_bounded(reinterpret_cast<char*>(s_runtime.station_config.sta.ssid),
                 sizeof(s_runtime.station_config.sta.ssid),
                 config.ssid);
    copy_bounded(reinterpret_cast<char*>(s_runtime.station_config.sta.password),
                 sizeof(s_runtime.station_config.sta.password),
                 config.password);
    s_runtime.station_config.sta.threshold.authmode =
        config.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    s_runtime.station_config.sta.pmf_cfg.capable = true;
    s_runtime.station_config.sta.pmf_cfg.required = false;

    if (esp_wifi_set_config(WIFI_IF_STA, &s_runtime.station_config) != ESP_OK)
    {
        set_status_message("Set Wi-Fi config failed");
        return ConnectStartResult::Failed;
    }

    const bool replacing_active_attempt =
        s_runtime.connected || s_runtime.connecting;
    const esp_err_t disconnect_err =
        replacing_active_attempt ? esp_wifi_disconnect() : ESP_ERR_WIFI_NOT_CONNECT;
    s_runtime.intentional_disconnect_pending =
        replacing_active_attempt && disconnect_err == ESP_OK;
    if (disconnect_err != ESP_OK)
    {
        // Ignore disconnect failures before a fresh connect attempt.
    }
    cache_config(config);
    clear_connection_details();
    s_runtime.connect_deferred_for_resources = false;
    s_runtime.connecting = true;
    refresh_runtime_status_message();
    if (esp_wifi_connect() != ESP_OK)
    {
        s_runtime.connecting = false;
        set_status_message("Wi-Fi connect failed");
        return ConnectStartResult::Failed;
    }
    return ConnectStartResult::Started;
}

bool connect(const Config* override_config)
{
    cancel_profile_retry();
    if (override_config)
    {
        reset_automatic_profile_failure_cycle();
        if (s_runtime.connecting && same_ssid(s_runtime.config, *override_config) &&
            std::strncmp(s_runtime.config.password,
                         override_config->password,
                         sizeof(s_runtime.config.password)) == 0)
        {
            return true;
        }
        const ConnectStartResult result = connect_single_profile(*override_config);
        if (result != ConnectStartResult::Started)
        {
            schedule_profile_retry();
        }
        return result == ConnectStartResult::Started;
    }

    Config config{};
    (void)load_config(config);
    if (!config.enabled)
    {
        set_status_message("Enable Wi-Fi first");
        return false;
    }
    if (s_runtime.profile_count == 0)
    {
        set_status_message("SSID is not set");
        return false;
    }

    if (s_runtime.connected || s_runtime.connecting)
    {
        return true;
    }

    // Automatic connection is event-driven. The old blocking scan and
    // 15-second wait ran in the LVGL owner and made resource deferral look
    // like a bad profile.
    const std::size_t index =
        s_runtime.next_profile_index % s_runtime.profile_count;
    Config candidate = s_runtime.profiles[index];
    candidate.enabled = config.enabled;
    std::printf("[WiFi] auto connect profile index=%u/%u ssid=%s\n",
                static_cast<unsigned>(index + 1U),
                static_cast<unsigned>(s_runtime.profile_count),
                candidate.ssid);
    s_runtime.automatic_profile_attempt_active = false;
    const ConnectStartResult result = connect_single_profile(candidate);
    if (result == ConnectStartResult::Started)
    {
        s_runtime.automatic_profile_attempt_active = true;
        return true;
    }
    if (result == ConnectStartResult::DeferredForResources)
    {
        std::printf("[WiFi] auto connect deferred profile index=%u/%u reason=resources\n",
                    static_cast<unsigned>(index + 1U),
                    static_cast<unsigned>(s_runtime.profile_count));
        schedule_profile_retry();
        return false;
    }
    if (result == ConnectStartResult::NotAttempted)
    {
        std::printf("[WiFi] auto connect deferred profile index=%u/%u reason=not_attempted\n",
                    static_cast<unsigned>(index + 1U),
                    static_cast<unsigned>(s_runtime.profile_count));
        schedule_profile_retry();
        return false;
    }
    note_automatic_profile_failure(index, "start");
    return false;
}

void disconnect()
{
    reset_automatic_profile_failure_cycle();
    cancel_profile_retry();
    if (s_runtime.wifi_started)
    {
        s_runtime.intentional_disconnect_pending =
            esp_wifi_disconnect() == ESP_OK;
    }
    clear_connection_details();
    refresh_runtime_status_message();
}

bool scan(ScanResult* out_results, std::size_t capacity, std::size_t& out_count)
{
    out_count = 0;

    Config config{};
    (void)load_config(config);
    if (!config.enabled)
    {
        set_status_message("Enable Wi-Fi first");
        return false;
    }

    if (!ensure_wifi_started())
    {
        return false;
    }

    s_runtime.scanning = true;
    refresh_runtime_status_message();

    wifi_scan_config_t scan_config{};
    const esp_err_t scan_err = esp_wifi_scan_start(&scan_config, true);
    s_runtime.scanning = false;
    if (scan_err != ESP_OK)
    {
        set_status_message("Wi-Fi scan failed");
        return false;
    }

    uint16_t ap_count = 0;
    if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK || ap_count == 0)
    {
        set_status_message("No Wi-Fi networks found");
        return true;
    }

    uint16_t record_count = std::min<uint16_t>(ap_count, kWifiAutoScanMaxRecords);
    for (wifi_ap_record_t& record : s_runtime.auto_scan_records)
    {
        record = wifi_ap_record_t{};
    }
    if (esp_wifi_scan_get_ap_records(&record_count, s_runtime.auto_scan_records) != ESP_OK)
    {
        set_status_message("Read scan results failed");
        return false;
    }

    for (uint16_t i = 0; i < record_count; ++i)
    {
        insert_scan_result_sorted(out_results, capacity, out_count, s_runtime.auto_scan_records[i]);
    }

    char buffer[kMaxStatusMessageLength + 1];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "Found %u networks",
                  static_cast<unsigned>(out_count));
    set_status_message(buffer);
    return true;
}

Status status()
{
    Status out{};
    out.supported = is_supported();
    if (!out.supported)
    {
        copy_bounded(out.message, sizeof(out.message), "Wi-Fi unsupported");
        out.state = ConnectionState::Unsupported;
        return out;
    }

    if (s_runtime.externally_suspended)
    {
        out.state = ConnectionState::ResourceDeferred;
        copy_bounded(out.message, sizeof(out.message), "Wi-Fi suspended for USB Disk");
        return out;
    }

    const Config& config = current_config();
    out.enabled = config.enabled;
    out.has_credentials = has_saved_credentials(config);
    out.connected = s_runtime.connected;
    out.scanning = s_runtime.scanning;
    out.rssi = s_runtime.rssi;
    copy_bounded(out.ssid,
                 sizeof(out.ssid),
                 s_runtime.connected ? s_runtime.ssid : config.ssid);
    copy_bounded(out.ip, sizeof(out.ip), s_runtime.ip);

    if (!config.enabled)
    {
        out.state = ConnectionState::Disabled;
        copy_bounded(out.message, sizeof(out.message), "Wi-Fi disabled");
        return out;
    }

    if (s_runtime.scanning)
    {
        out.state = ConnectionState::Scanning;
        copy_bounded(out.message, sizeof(out.message), "Scanning...");
        return out;
    }

    if (s_runtime.connecting)
    {
        out.state = ConnectionState::Connecting;
        copy_bounded(out.message, sizeof(out.message), "Connecting...");
        return out;
    }

    if (s_runtime.connect_deferred_for_resources)
    {
        out.state = ConnectionState::ResourceDeferred;
        copy_bounded(out.message,
                     sizeof(out.message),
                     "Wi-Fi waiting for memory");
        return out;
    }

    if (s_runtime.connected)
    {
        out.state = ConnectionState::Connected;
        copy_bounded(out.message, sizeof(out.message), s_runtime.message);
        return out;
    }

    out.state = disconnected_state(config.enabled, out.has_credentials);
    if (out.has_credentials)
    {
        copy_bounded(out.message, sizeof(out.message), "Ready to connect");
    }
    else
    {
        copy_bounded(out.message, sizeof(out.message), "Set SSID and password");
    }
    return out;
}

} // namespace platform::ui::wifi
