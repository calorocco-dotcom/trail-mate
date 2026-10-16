#include "platform/ui/wifi_runtime.h"

#include "platform/ui/settings_store.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace platform::ui::wifi
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr const char* kUnsupportedMessage = "Wi-Fi unsupported on Linux Cardputer Zero";

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

bool has_credentials(const Config& config)
{
    return config.ssid[0] != '\0';
}

} // namespace

bool is_supported()
{
    return false;
}

bool load_config(Config& out)
{
    out = Config{};
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

    return true;
}

bool save_config(const Config& config)
{
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, config.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, config.enabled);
    return ssid_ok && password_ok;
}

bool find_saved_config(const char* ssid, Config& out)
{
    out = Config{};
    Config config{};
    (void)load_config(config);
    if (ssid && ssid[0] != '\0' && std::strcmp(config.ssid, ssid) == 0)
    {
        out = config;
        return true;
    }
    return false;
}

bool apply_enabled(bool enabled)
{
    return !enabled;
}

bool set_user_enabled(bool enabled) { return apply_enabled(enabled); }

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

bool connect(const Config*)
{
    return false;
}

void disconnect()
{
}

bool scan(ScanResult* out_results, std::size_t capacity, std::size_t& out_count)
{
    (void)out_results;
    (void)capacity;
    out_count = 0;
    return false;
}

Status status()
{
    Config config{};
    (void)load_config(config);

    Status out{};
    out.supported = false;
    out.enabled = false;
    out.connected = false;
    out.scanning = false;
    out.has_credentials = has_credentials(config);
    out.rssi = -127;
    out.state = ConnectionState::Unsupported;
    copy_text(out.ssid, sizeof(out.ssid), config.ssid);
    copy_text(out.message, sizeof(out.message), kUnsupportedMessage);
    return out;
}

} // namespace platform::ui::wifi
