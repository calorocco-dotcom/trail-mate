#include "platform/ui/wifi_access_runtime.h"
#include <array>
#include <cassert>
#include <cstring>

namespace
{
bool saved = true, storage_ok = true, present = true, supported = true;
bool transport = true, configure_ok = true, clients_ok = true;
unsigned writes = 0, configurations = 0, revocations = 0, resets = 0;
platform::ui::wifi_access::RuntimeStatus access{};
} // namespace
namespace platform::ui::wifi_access
{
RuntimeStatus status() { return access; }
bool set_transport_enabled(bool enabled)
{
    if (!enabled) ++revocations;
    transport = enabled;
    return clients_ok;
}
} // namespace platform::ui::wifi_access
namespace platform::ui::settings_store
{
void put_bool(const char* ns, const char* key, bool value)
{
    assert(std::strcmp(ns, "settings") == 0 && std::strcmp(key, "wifi_enabled") == 0);
    ++writes;
    if (storage_ok) saved = value;
}
bool get_bool(const char*, const char*, bool) { return saved; }
} // namespace platform::ui::settings_store
namespace platform::ui::wifi
{
struct Config
{
    bool enabled = true;
};
struct Runtime
{
    bool config_cached = true;
    Config saved_config{};
    std::array<const char*, 2> profiles{"first-network", "second-network"};
} s_runtime;
constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
bool is_supported() { return supported; }
bool c6_present() { return present; }
void load_saved_config() { s_runtime.config_cached = true; }
bool make_companion_wifi_config(const Config& config) { return config.enabled; }
void reset_automatic_profile_attempts() { ++resets; }
namespace c6
{
struct Companion
{
    bool requested = true;
    bool configureWifi(bool enabled)
    {
        if (!enabled) assert(!transport); // Clients must release first.
        requested = enabled;
        ++configurations;
        return configure_ok;
    }
} companion;
Companion& c6_companion() { return companion; }
} // namespace c6
#include "c6_user_switch.inc"
} // namespace platform::ui::wifi
int main()
{
    using namespace platform::ui;
    const auto profiles = wifi::s_runtime.profiles;
    assert(wifi::set_user_enabled(false));
    assert(!saved && !transport && !wifi::c6::companion.requested && revocations == 1);
    assert(wifi::set_user_enabled(true));
    assert(saved && transport && wifi::c6::companion.requested && resets == 2);
    assert(wifi::s_runtime.profiles == profiles);
    const auto before = writes;
    access.ota_active = true;
    assert(!wifi::set_user_enabled(false));
    access.ota_active = false;
    access.non_preemptible_active = true;
    assert(!wifi::set_user_enabled(false));
    access.non_preemptible_active = false;
    access.exclusive_owner = wifi_access::ExclusiveOwner::ReticulumCallAudio;
    assert(!wifi::set_user_enabled(false));
    access.exclusive_owner = wifi_access::ExclusiveOwner::None;
    assert(writes == before);
    storage_ok = false;
    assert(!wifi::set_user_enabled(false));
    assert(saved && transport);
    storage_ok = true;
    configure_ok = false;
    assert(!wifi::set_user_enabled(false));
    assert(saved && transport && wifi::c6::companion.requested);
    configure_ok = true;
    clients_ok = false;
    assert(!wifi::set_user_enabled(false));
    assert(saved && transport && wifi::c6::companion.requested);
    clients_ok = true;
    present = false;
    const auto offline_writes = writes;
    assert(!wifi::set_user_enabled(true));
    assert(writes == offline_writes);
    assert(wifi::set_user_enabled(false));
    assert(!saved && !transport && wifi::s_runtime.profiles == profiles);
}
