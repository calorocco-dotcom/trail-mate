#include "platform/ui/wifi_runtime.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Storage and runtime helpers are simulated; the four persistence/migration
// functions below are extracted unchanged from both production backends.
namespace platform::ui::settings_store
{
std::map<std::string, std::string> text;
std::map<std::string, int> scalar;
std::vector<std::string> writes;
std::string failed_write, failed_read;
bool uncertain_string_write = false;
struct ScopedChangeBatch
{
    ~ScopedChangeBatch() {}
};
bool put_string(const char*, const char* key, const char* value)
{
    writes.emplace_back(key);
    if (key != failed_write || uncertain_string_write) text[key] = value;
    return key != failed_write;
}
bool get_string(const char*, const char* key, std::string& value)
{
    const auto found = text.find(key);
    if (key == failed_read || found == text.end()) return false;
    value = found->second;
    return true;
}
void put_int(const char*, const char* key, int value)
{
    writes.emplace_back(key);
    if (key != failed_write) scalar[key] = value;
}
int get_int(const char*, const char* key, int fallback)
{
    const auto found = scalar.find(key);
    return key == failed_read || found == scalar.end() ? fallback : found->second;
}
void put_bool(const char* ns, const char* key, bool value) { put_int(ns, key, value); }
bool get_bool(const char* ns, const char* key, bool fallback) { return get_int(ns, key, fallback) != 0; }
void remove_keys(const char*, const char* const* keys, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) text.erase(keys[i]);
}
} // namespace platform::ui::settings_store

using platform::ui::wifi::Config;
namespace store = platform::ui::settings_store;
constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiProfileCountKey = "wifi_prof_count";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr std::size_t kWifiProfileCapacity = platform::ui::wifi::kMaxSavedProfileCount;
struct RuntimeFixture
{
    Config profiles[kWifiProfileCapacity]{};
    Config config{}, saved_config{};
    std::size_t profile_count = 0, next_profile_index = 0, automatic_profile_attempts = 0;
    bool profiles_cached = true, config_cached = true;
} s_runtime;
void profile_key(char* out, std::size_t size, const char* field, std::size_t index)
{
    std::snprintf(out, size, "wifi_%s_%u", field, static_cast<unsigned>(index));
}
void copy_bounded(char* out, std::size_t size, const char* value) { std::snprintf(out, size, "%s", value); }
void copy_text(char* out, std::size_t size, const char* value) { copy_bounded(out, size, value); }
bool has_saved_credentials(const Config& value) { return value.ssid[0] != '\0'; }
bool same_ssid(const Config& lhs, const Config& rhs) { return !std::strcmp(lhs.ssid, rhs.ssid); }
void clear_profiles()
{
    for (auto& profile : s_runtime.profiles) profile = {};
    s_runtime.profile_count = 0;
}
void upsert_profile_front(const Config& config)
{
    s_runtime.profiles[0] = config;
    s_runtime.profile_count = 1;
}
void cache_config(const Config& config)
{
    s_runtime.config = config;
    s_runtime.config_cached = true;
}
void refresh_runtime_status_message() {}
bool read_config_from_store(Config&) { return true; }
Config load_saved_config() { return s_runtime.saved_config; }

#if defined(WIFI_STORAGE_IDF)
#include "wifi_idf_persist_actual.inc"
#include "wifi_idf_read_actual.inc"
#include "wifi_idf_replace_actual.inc"
#include "wifi_idf_save_actual.inc"
#else
#include "wifi_common_persist_actual.inc"
#include "wifi_common_read_actual.inc"
#include "wifi_common_replace_actual.inc"
#include "wifi_common_save_actual.inc"
#endif

void testWifiStorageFailures()
{
    const auto reset = []
    {
        s_runtime = {};
        store::text.clear();
        store::scalar.clear();
        store::writes.clear();
        store::failed_write.clear();
        store::failed_read.clear();
        store::uncertain_string_write = false;
    };
    Config profile{};
    copy_bounded(profile.ssid, sizeof(profile.ssid), "trail-test");
    copy_bounded(profile.password, sizeof(profile.password), "fixture-password");
    const auto save = [&](bool replace)
    {
        if (replace) return ::replace_saved_profiles(profile.enabled, &profile, 1);
#if defined(WIFI_STORAGE_IDF)
        return save_saved_config(profile);
#else
        return ::save_config(profile);
#endif
    };
    for (const bool replace : {false, true})
    {
        for (const bool enabled : {false, true})
        {
            profile.enabled = enabled;
            reset();
            assert(save(replace));
            assert(store::scalar.at(kWifiProfileCountKey) == 1);
            assert(store::scalar.at(kWifiEnabledKey) == enabled);
            assert(store::text.at("wifi_password_0") == profile.password);
            assert(s_runtime.config_cached && s_runtime.profiles_cached);
            const auto keys = store::writes;
            assert(keys.size() == 24);
            assert(keys[20] == kWifiProfileCountKey); // All 20 slots committed first.
            for (const auto& key : keys)
            {
                reset();
                store::scalar[kWifiEnabledKey] = !enabled;
                store::scalar[kWifiProfileCountKey] = 0;
                store::failed_write = key;
                assert(!save(replace));
                assert(!s_runtime.config_cached && !s_runtime.profiles_cached);
                if (key.find("_0") != std::string::npos || key.find("_9") != std::string::npos)
                    assert(store::scalar.at(kWifiProfileCountKey) == 0);
            }
            for (const auto* key : {kWifiProfileCountKey, kWifiEnabledKey})
            {
                reset();
                store::failed_read = key;
                assert(!save(replace));
                assert(!s_runtime.config_cached && !s_runtime.profiles_cached);
            }
        }
    }
    reset();
    assert(::replace_saved_profiles(false, nullptr, 0));
    assert(store::scalar.at(kWifiProfileCountKey) == 0);
    assert(store::text.at(kWifiSsidKey).empty() && store::text.at(kWifiPasswordKey).empty());
    for (const bool password : {false, true})
    {
        reset();
        auto invalid = profile;
        if (password) std::memset(invalid.password, 'x', sizeof(invalid.password));
        else std::memset(invalid.ssid, 'x', sizeof(invalid.ssid));
        assert(!::replace_saved_profiles(true, &invalid, 1));
        assert(store::writes.empty());
    }
    reset();
    const Config duplicate[] = {profile, profile};
    assert(!::replace_saved_profiles(true, duplicate, 2));
    assert(!::replace_saved_profiles(true, nullptr, 1));
    assert(!::replace_saved_profiles(true, &profile, kWifiProfileCapacity + 1));
    assert(store::writes.empty());
}

void testWifiLegacyMigration()
{
    for (const bool uncertain : {false, true})
    {
        store::text = {{"wifi_ssid_0", "legacy-fixture"}, {"wifi_pass_0", "legacy-password"}};
        store::failed_read.clear();
        store::failed_write = "wifi_password_0";
        store::uncertain_string_write = uncertain;
        Config loaded{};
        assert(read_profile_from_store(0, loaded));
        assert(!std::strcmp(loaded.password, "legacy-password"));
        assert(store::text.at("wifi_pass_0") == "legacy-password");
    }
    store::text = {{"wifi_ssid_0", "legacy-fixture"}, {"wifi_pass_0", "legacy-password"}};
    store::failed_write.clear();
    store::uncertain_string_write = false;
    Config loaded{};
    assert(read_profile_from_store(0, loaded));
    assert(store::text.at("wifi_password_0") == "legacy-password");
    assert(!store::text.count("wifi_pass_0"));
    store::text["wifi_pass_0"] = "old-password";
    assert(read_profile_from_store(0, loaded));
    assert(!std::strcmp(loaded.password, "legacy-password"));
    assert(store::text.at("wifi_pass_0") == "old-password"); // Existing new key wins.
}

int main()
{
    testWifiStorageFailures();
    testWifiLegacyMigration();
}
