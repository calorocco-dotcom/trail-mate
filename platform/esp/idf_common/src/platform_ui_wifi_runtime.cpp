#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include "platform/esp/common/wifi_profile_snapshot.h"
#include "platform/ui/wifi_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "hostlink/c6/c6_protocol.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_access_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)
#define TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN 1
#endif

#if defined(ESP_PLATFORM) && defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace platform::ui::wifi
{
namespace
{

namespace c6 = ::platform::esp::idf_common::wireless_companion;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr const char* kWifiProfileCountKey = "wifi_prof_count";
constexpr std::size_t kWifiProfileCapacity = 10;
constexpr uint32_t kWifiFeatureMask = TM_C6_FEATURE_WIFI_STA | TM_C6_FEATURE_WIFI_AP;
#if defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
constexpr uint32_t kC6ScanWaitTimeoutMs = 10000;
constexpr uint32_t kC6ScanPollIntervalMs = 100;
#endif

struct RuntimeState
{
    bool config_cached = false;
    bool profiles_cached = false;
    Config saved_config{};
    Config profiles[kWifiProfileCapacity] = {};
    ScanResult scan_results[kWifiProfileCapacity] = {};
    std::size_t profile_count = 0;
    std::size_t next_profile_index = 0;
    std::size_t automatic_profile_attempts = 0;
};

RuntimeState s_runtime{};

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!text)
    {
        out[0] = '\0';
        return;
    }
    std::size_t copy_len = std::strlen(text);
    if (copy_len >= out_len)
    {
        copy_len = out_len - 1U;
    }
    std::memmove(out, text, copy_len);
    out[copy_len] = '\0';
}

void copy_config_text(char* out, std::size_t out_len, const char* text)
{
    copy_text(out, out_len, text && text[0] != '\0' ? text : "");
}

bool has_saved_credentials(const Config& config)
{
    return config.ssid[0] != '\0';
}

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
    copy_text(profile.ssid, sizeof(profile.ssid), value.c_str());
    if (!has_saved_credentials(profile))
    {
        return false;
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, password_key, value))
    {
        copy_text(profile.password, sizeof(profile.password), value.c_str());
    }
    else
    {
        char legacy_password_key[24] = {};
        profile_key(legacy_password_key, sizeof(legacy_password_key), "pass", index);
        value.clear();
        if (::platform::ui::settings_store::get_string(kSettingsNs, legacy_password_key, value))
        {
            copy_text(profile.password, sizeof(profile.password), value.c_str());
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

void format_ipv4(uint32_t addr, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (addr == 0)
    {
        out[0] = '\0';
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%u.%u.%u.%u",
                  static_cast<unsigned>(addr & 0xffu),
                  static_cast<unsigned>((addr >> 8) & 0xffu),
                  static_cast<unsigned>((addr >> 16) & 0xffu),
                  static_cast<unsigned>((addr >> 24) & 0xffu));
}

Config load_saved_config()
{
    Config out{};
    out.enabled = ::platform::ui::settings_store::get_bool(kSettingsNs, kWifiEnabledKey, false);

    std::string value;
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiSsidKey, value))
    {
        copy_text(out.ssid, sizeof(out.ssid), value.c_str());
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiPasswordKey, value))
    {
        copy_text(out.password, sizeof(out.password), value.c_str());
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
        std::printf("[WiFi][C6] recovered saved profile count=%u from profile keys\n",
                    static_cast<unsigned>(s_runtime.profile_count));
        (void)persist_profiles();
    }
    clamp_next_profile_index();
    if (!has_primary_credentials && s_runtime.profile_count > 0)
    {
        copy_text(out.ssid, sizeof(out.ssid), s_runtime.profiles[0].ssid);
        copy_text(out.password, sizeof(out.password), s_runtime.profiles[0].password);
    }
    s_runtime.saved_config = out;
    s_runtime.config_cached = true;
    return out;
}

Config cached_saved_config()
{
    if (!s_runtime.config_cached)
    {
        return load_saved_config();
    }
    return s_runtime.saved_config;
}

bool save_saved_config(const Config& config)
{
    if (!s_runtime.profiles_cached)
    {
        (void)load_saved_config();
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
    const bool saved = profiles_ok && ssid_ok && password_ok && enabled_ok;
    if (saved)
    {
        s_runtime.saved_config = config;
        s_runtime.config_cached = true;
    }
    else
    {
        s_runtime.profiles_cached = false;
        s_runtime.config_cached = false;
    }
    return saved;
}

c6::WifiCompanionConfig make_companion_wifi_config(const Config& config)
{
    c6::WifiCompanionConfig out{};
    out.enabled = config.enabled;
    out.sta_enabled = config.enabled && config.ssid[0] != '\0';
    out.ap_enabled = false;
    out.persist_credentials = false;
    copy_config_text(out.sta_ssid, sizeof(out.sta_ssid), config.ssid);
    copy_config_text(out.sta_password, sizeof(out.sta_password), config.password);
    copy_text(out.ap_ssid, sizeof(out.ap_ssid), "TrailMate-C6");
    out.ap_channel = 1;
    return out;
}

c6::WifiControl make_control(c6::WifiCommand command, const Config* config = nullptr)
{
    c6::WifiControl out{};
    out.command = command;
    if (config)
    {
        copy_config_text(out.ssid, sizeof(out.ssid), config->ssid);
        copy_config_text(out.password, sizeof(out.password), config->password);
    }
    return out;
}

bool c6_present()
{
    return c6::get_c6_companion_status().present;
}

void reset_automatic_profile_attempts()
{
    s_runtime.automatic_profile_attempts = 0;
}

void disable_wifi_after_profile_exhaustion()
{
    Config disabled_config = cached_saved_config();
    disabled_config.enabled = false;
    const bool persisted = save_saved_config(disabled_config);
    const bool stopped = apply_enabled(false);
    std::printf("[WiFi][C6] saved profiles exhausted action=wifi_off persisted=%u stopped=%u\n",
                persisted ? 1U : 0U,
                stopped ? 1U : 0U);
    reset_automatic_profile_attempts();
}

std::size_t copy_scan_results(ScanResult* out_results,
                              std::size_t capacity,
                              const c6::C6CompanionStatus& c6_status)
{
    if (out_results == nullptr || capacity == 0)
    {
        return 0;
    }

    std::size_t count = 0;
    for (uint8_t i = 0; i < c6_status.wifi_scan_result_count && count < capacity; ++i)
    {
        ScanResult& result = out_results[count];
        result = ScanResult{};
        copy_text(result.ssid, sizeof(result.ssid), c6_status.wifi_scan_results[i].ssid);
        result.rssi = c6_status.wifi_scan_results[i].rssi;
        result.requires_password = c6_status.wifi_scan_results[i].authmode != 0;
        ++count;
    }
    return count;
}

bool wait_for_c6_scan(ScanResult* out_results, std::size_t capacity, std::size_t& out_count)
{
    out_count = 0;
#if defined(ESP_PLATFORM) && defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(kC6ScanWaitTimeoutMs);
    bool saw_scanning = true;

    while ((xTaskGetTickCount() - start_ticks) < timeout_ticks)
    {
        const auto c6_status = c6::get_c6_companion_status();
        if (!c6_status.present)
        {
            return false;
        }
        if (c6_status.wifi_scanning)
        {
            saw_scanning = true;
        }
        else if (saw_scanning || c6_status.wifi_scan_result_count > 0)
        {
            out_count = copy_scan_results(out_results, capacity, c6_status);
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(kC6ScanPollIntervalMs));
    }

    const auto c6_status = c6::get_c6_companion_status();
    out_count = copy_scan_results(out_results, capacity, c6_status);
    return out_count > 0;
#else
    (void)out_results;
    (void)capacity;
    return false;
#endif
}

bool c6_wifi_ready_for_config(const c6::C6CompanionStatus& status, const Config& config)
{
    if (!status.wifi_connected || status.wifi_ipv4_addr == 0)
    {
        return false;
    }
    return status.wifi_ssid[0] == '\0' ||
           std::strncmp(status.wifi_ssid, config.ssid, sizeof(status.wifi_ssid)) == 0;
}

} // namespace

bool is_supported()
{
    return c6::get_c6_companion_status().board_capable;
}

bool load_config(Config& out)
{
    out = load_saved_config();
    return true;
}

bool save_config(const Config& config)
{
    return save_saved_config(config);
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
        (void)load_saved_config();
    }
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (std::strncmp(s_runtime.profiles[i].ssid, ssid, sizeof(s_runtime.profiles[i].ssid)) == 0)
        {
            out = s_runtime.profiles[i];
            return true;
        }
    }
    return false;
}

bool visit_saved_profiles(bool* enabled, std::size_t* count, SavedProfileVisitor visitor, void* context)
{
    if (!enabled || !count || !visitor) return false;
    if (!s_runtime.profiles_cached || !s_runtime.config_cached) (void)load_saved_config();
    *enabled = s_runtime.saved_config.enabled;
    *count = s_runtime.profile_count;
    for (std::size_t index = 0; index < s_runtime.profile_count; ++index)
    {
        if (!visitor(context, index, s_runtime.profiles[index])) return false;
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
    if (count > kWifiProfileCapacity || (count && !profiles)) return false;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!std::memchr(profiles[index].ssid, '\0', sizeof(profiles[index].ssid)) ||
            !std::memchr(profiles[index].password, '\0', sizeof(profiles[index].password)) ||
            !has_saved_credentials(profiles[index])) return false;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (same_ssid(profiles[index], profiles[previous])) return false;
        }
    }
    ::platform::ui::settings_store::ScopedChangeBatch changes;
    // Reuse the existing runtime profile array; the validated TMS staging set
    // belongs to the caller and is never copied into an automatic array.
    clear_profiles();
    for (std::size_t index = 0; index < count; ++index)
    {
        s_runtime.profiles[index] = profiles[index];
        s_runtime.profiles[index].enabled = true;
    }
    s_runtime.profile_count = count;
    s_runtime.next_profile_index = 0;
    s_runtime.automatic_profile_attempts = 0;
    s_runtime.saved_config = count ? s_runtime.profiles[0] : Config{};
    s_runtime.saved_config.enabled = enabled;
    s_runtime.config_cached = true;
    const bool profiles_ok = persist_profiles();
    const bool ssid_ok = ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, s_runtime.saved_config.ssid);
    const bool password_ok = ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, s_runtime.saved_config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, enabled);
    const bool enabled_ok = ::platform::ui::settings_store::get_bool(
                                kSettingsNs, kWifiEnabledKey, !enabled) == enabled;
    const bool saved = profiles_ok && ssid_ok && password_ok && enabled_ok;
    if (!saved)
    {
        s_runtime.profiles_cached = false;
        s_runtime.config_cached = false;
    }
    return saved;
}

bool apply_enabled(bool enabled)
{
    reset_automatic_profile_attempts();
    Config config = load_saved_config();
    config.enabled = enabled;
    const bool saved = save_saved_config(config);
    if (!c6_present())
    {
        return saved && !enabled;
    }
    return saved && c6::c6_companion().configureWifi(make_companion_wifi_config(config));
}

bool set_user_enabled(bool enabled)
{
    const auto access = wifi_access::status();
    if (!is_supported() || access.non_preemptible_active || access.ota_active ||
        access.exclusive_owner != wifi_access::ExclusiveOwner::None || (enabled && !c6_present()))
        return false;
    if (!s_runtime.config_cached) (void)load_saved_config();
    const bool previous = s_runtime.saved_config.enabled;
    const auto persist = [](bool value)
    {
        settings_store::put_bool(kSettingsNs, kWifiEnabledKey, value);
        if (settings_store::get_bool(kSettingsNs, kWifiEnabledKey, !value) != value) return false;
        s_runtime.saved_config.enabled = value;
        return true;
    };
    // A toggle edits only the enabled preference, preserving credentials and
    // profile order. Revoke client leases before stopping the C6 transport.
    if (!persist(enabled)) return false;
    const bool clients_stopped = enabled || wifi_access::set_transport_enabled(false);
    const bool configured = clients_stopped && (!c6_present() ||
                                                c6::c6_companion().configureWifi(make_companion_wifi_config(s_runtime.saved_config)));
    if (configured && (!enabled || wifi_access::set_transport_enabled(true)))
    {
        reset_automatic_profile_attempts();
        return true;
    }
    (void)persist(previous);
    // Restore the C6's requested configuration even when transmission failed.
    s_runtime.saved_config.enabled = previous;
    if (c6_present()) (void)c6::c6_companion().configureWifi(make_companion_wifi_config(s_runtime.saved_config));
    (void)wifi_access::set_transport_enabled(previous);
    return false;
}

bool suspend_for_external_storage(ExternalStorageSuspension* out_suspension)
{
    if (out_suspension)
    {
        *out_suspension = ExternalStorageSuspension{};
    }
    return true;
}

void resume_after_external_storage(ExternalStorageSuspension* suspension)
{
    if (suspension)
    {
        *suspension = ExternalStorageSuspension{};
    }
}

bool connect(const Config* override_config)
{
    Config config = override_config ? *override_config : load_saved_config();
    if (!override_config && !config.enabled)
    {
        std::printf("[WiFi][C6] connect rejected reason=wifi_disabled\n");
        return false;
    }
    if (override_config)
    {
        reset_automatic_profile_attempts();
    }

    const auto before = c6::get_c6_companion_status();
    if (!before.present)
    {
        std::printf("[WiFi][C6] connect rejected reason=c6_not_present board_capable=%u "
                    "started=%u state=%s detail=%s features=0x%08lX\n",
                    before.board_capable ? 1U : 0U,
                    before.started ? 1U : 0U,
                    c6::companion_state_name(before.state),
                    before.detail ? before.detail : "unknown",
                    static_cast<unsigned long>(before.supported_features));
        return false;
    }

    if (!override_config && s_runtime.profile_count > 0)
    {
        if (before.wifi_connected)
        {
            // Once an SSID is connected, only a Wi-Fi association failure may
            // advance profiles. MQTT and other client failures never do.
            reset_automatic_profile_attempts();
            return true;
        }
        if (s_runtime.automatic_profile_attempts >= s_runtime.profile_count)
        {
            disable_wifi_after_profile_exhaustion();
            return false;
        }
        std::size_t index = s_runtime.next_profile_index % s_runtime.profile_count;
        config = s_runtime.profiles[index];
        config.enabled = true;
        std::printf("[WiFi][C6] auto connect profile index=%u/%u ssid=%s\n",
                    static_cast<unsigned>(index + 1U),
                    static_cast<unsigned>(s_runtime.profile_count),
                    config.ssid);
        s_runtime.next_profile_index = (index + 1U) % s_runtime.profile_count;
        ++s_runtime.automatic_profile_attempts;
    }
    config.enabled = true;
    if (override_config && !save_saved_config(config))
    {
        std::printf("[WiFi][C6] connect rejected reason=config_save_failed ssid=%s\n",
                    config.ssid);
        return false;
    }
    if (!has_saved_credentials(config))
    {
        std::printf("[WiFi][C6] connect rejected reason=no_credentials ssid_len=%u\n",
                    static_cast<unsigned>(std::strlen(config.ssid)));
        return false;
    }
    if (c6_wifi_ready_for_config(before, config))
    {
        return true;
    }
    if (!c6::c6_companion().configureWifi(make_companion_wifi_config(config)))
    {
        const auto after = c6::get_c6_companion_status();
        std::printf("[WiFi][C6] connect rejected reason=config_failed state=%s detail=%s "
                    "config_error=%u wifi_state=%u wifi_error=%u events=%lu\n",
                    c6::companion_state_name(after.state),
                    after.detail ? after.detail : "unknown",
                    static_cast<unsigned>(after.config_error),
                    static_cast<unsigned>(after.wifi_state),
                    static_cast<unsigned>(after.wifi_error),
                    static_cast<unsigned long>(after.wifi_event_count));
        return false;
    }
    const bool sent =
        c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Connect, &config));
    const auto after = c6::get_c6_companion_status();
    std::printf("[WiFi][C6] connect request sent=%u ssid=%s state=%s detail=%s "
                "wifi_state=%u connected=%u wifi_error=%u events=%lu\n",
                sent ? 1U : 0U,
                config.ssid,
                c6::companion_state_name(after.state),
                after.detail ? after.detail : "unknown",
                static_cast<unsigned>(after.wifi_state),
                after.wifi_connected ? 1U : 0U,
                static_cast<unsigned>(after.wifi_error),
                static_cast<unsigned long>(after.wifi_event_count));
    // Connection completion is observed through status(); never wait for C6
    // events in the LVGL caller.
    return sent;
}

void disconnect()
{
    if (c6_present())
    {
        (void)c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Disconnect));
    }
}

bool scan(ScanResult* out_results, std::size_t capacity, std::size_t& out_count)
{
    out_count = 0;
    const auto c6_status = c6::get_c6_companion_status();
    out_count = copy_scan_results(out_results, capacity, c6_status);
    if (!c6_status.present)
    {
        return out_count > 0;
    }
    const bool sent = c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Scan));
    if (!sent)
    {
        return false;
    }
#if defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
    return wait_for_c6_scan(out_results, capacity, out_count);
#else
    return true;
#endif
}

Status status()
{
    const auto c6_status = c6::get_c6_companion_status();
    const Config config = cached_saved_config();
    if (c6_status.wifi_connected)
    {
        reset_automatic_profile_attempts();
    }
    Status out{};
    out.supported = c6_status.board_capable;
    out.enabled = config.enabled;
    out.connected = c6_status.wifi_connected;
    out.scanning = c6_status.wifi_scanning;
    out.has_credentials = config.ssid[0] != '\0';
    out.rssi = -127;
    copy_text(out.ssid, sizeof(out.ssid), c6_status.wifi_ssid[0] != '\0' ? c6_status.wifi_ssid : config.ssid);
    format_ipv4(c6_status.wifi_ipv4_addr, out.ip, sizeof(out.ip));
    if (!c6_status.board_capable)
    {
        out.state = ConnectionState::Unsupported;
        copy_text(out.message, sizeof(out.message), "Wi-Fi unsupported on this device");
    }
    else if (!c6_status.present)
    {
        out.state = ConnectionState::Error;
        copy_text(out.message, sizeof(out.message), "Wi-Fi hardware unavailable");
    }
    else if ((c6_status.supported_features & kWifiFeatureMask) == 0)
    {
        out.state = ConnectionState::Unsupported;
        copy_text(out.message, sizeof(out.message), "Wi-Fi unavailable");
    }
    else if (!config.enabled)
    {
        out.state = ConnectionState::Disabled;
        copy_text(out.message, sizeof(out.message), "Wi-Fi disabled");
    }
    else if (c6_status.wifi_scanning)
    {
        out.state = ConnectionState::Scanning;
        copy_text(out.message, sizeof(out.message), "Scanning...");
    }
    else if (c6_status.wifi_connected)
    {
        out.state = ConnectionState::Connected;
        if (out.ip[0] != '\0')
        {
            std::snprintf(out.message, sizeof(out.message), "Connected %s", out.ip);
        }
        else
        {
            copy_text(out.message, sizeof(out.message), "Connected");
        }
    }
    else if (c6_status.wifi_error != 0)
    {
        out.state = ConnectionState::Error;
        copy_text(out.message, sizeof(out.message), "Wi-Fi error");
    }
    else
    {
        out.state = ConnectionState::Idle;
        copy_text(out.message,
                  sizeof(out.message),
                  out.has_credentials ? "Ready to connect" : "Set SSID and password");
    }
    return out;
}

} // namespace platform::ui::wifi

#else

#include "platform/esp/common/wifi_runtime_impl.h"

#endif
