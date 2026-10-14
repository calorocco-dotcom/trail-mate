#include "app/tms_c6_config_extension.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_runtime.h"
#include <cassert>
#include <map>
#include <string>
namespace tms = app::tms;
struct State
{
    tms::c6::Config companion;
    platform::ui::wifi::Config wifi_profiles[1];
    uint8_t timezone_tzdef[192]{};
    bool wifi_enabled = false;
} s_state;
std::map<std::string, int> numbers;
std::map<std::string, std::string> strings;
bool reject_write = false;
bool unavailable_read = false;
std::string failed_scalar;
platform::ui::settings_store::ScalarReadResult scalar_failure = platform::ui::settings_store::ScalarReadResult::Ok;
namespace platform::ui::settings_store
{
void begin_change_batch() {}
void end_change_batch() {}
void put_int(const char*, const char* key, int value)
{
    if (!reject_write) numbers[key] = value;
}
void put_bool(const char* ns, const char* key, bool value) { put_int(ns, key, value); }
int get_int(const char*, const char* key, int fallback)
{
    auto it = numbers.find(key);
    return it == numbers.end() ? fallback : it->second;
}
bool get_bool(const char* ns, const char* key, bool fallback) { return get_int(ns, key, fallback) != 0; }
ScalarReadResult read_int_into(const char*, const char* key, int32_t* out)
{
    *out = 0;
    if (failed_scalar == key) return scalar_failure;
    auto it = numbers.find(key);
    if (it == numbers.end()) return ScalarReadResult::Missing;
    *out = it->second;
    return ScalarReadResult::Ok;
}
ScalarReadResult read_bool_into(const char* ns, const char* key, bool* out)
{
    int32_t value = 0;
    auto result = read_int_into(ns, key, &value);
    *out = false;
    if (result != ScalarReadResult::Ok) return result;
    if (value != 0 && value != 1) return ScalarReadResult::Invalid;
    *out = value != 0;
    return ScalarReadResult::Ok;
}
bool put_string(const char*, const char* key, const char* value)
{
    if (reject_write) return false;
    strings[key] = value;
    return true;
}
bool get_string_into(const char*, const char* key, char* out, size_t capacity, size_t* length)
{
    auto it = strings.find(key);
    if (it == strings.end() || it->second.size() >= capacity) return false;
    *length = it->second.size();
    std::memcpy(out, it->second.c_str(), *length + 1);
    return true;
}
StringReadResult read_string_into(const char* ns, const char* key, char* out, size_t capacity, size_t* length)
{
    if (unavailable_read) return StringReadResult::Unavailable;
    auto it = strings.find(key);
    if (it == strings.end()) return StringReadResult::Missing;
    if (it->second.size() >= capacity) return StringReadResult::Invalid;
    return get_string_into(ns, key, out, capacity, length) ? StringReadResult::Ok : StringReadResult::Unavailable;
}
} // namespace platform::ui::settings_store
namespace platform::ui::wifi
{
bool load_config(Config& config)
{
    config = {};
    return true;
}
} // namespace platform::ui::wifi
#include "tms_c6_storage_actual.inc"
int main()
{
    auto& c = s_state.companion;
    c.seen = tms::c6::kComplete;
    c.wifi_ap_channel = 6;
    c.espnow_channel = 14;
    std::strcpy(c.wifi_sta_ssid, "Mountain");
    std::strcpy(c.wifi_sta_password, "private-value");
    assert(apply_c6_records());
    assert(numbers.size() == 10 && strings.size() == 4);
    assert(strings["wifi_sta_password"] == "private-value");
    c.wifi_ap_channel = 0;
    const auto before = numbers;
    assert(!apply_c6_records());
    assert(numbers == before);
    c.wifi_ap_channel = 7;
    reject_write = true;
    assert(!apply_c6_records());
    assert(numbers == before);
    reject_write = false;
    numbers["espnow_channel"] = 15;
    std::string output;
    tms::LineScratch scratch;
    tms::RecordWriter writer({&output, [](void* context, const char* bytes, size_t count)
                              {static_cast<std::string*>(context)->append(bytes,count);return true; }},
                             scratch, nullptr);
    assert(!write_c6_records(writer));
    assert(output.empty());
    numbers["espnow_channel"] = 14;
    strings["wifi_sta_password"] = std::string(64, 'x');
    assert(!write_c6_records(writer));
    assert(output.empty());
    strings["wifi_sta_password"] = "private-value";
    unavailable_read = true;
    assert(!write_c6_records(writer));
    assert(output.empty());
    unavailable_read = false;
    strings.clear();
    assert(write_c6_records(writer));
    assert(!output.empty());
    const char* scalar_keys[] = {"espnow_enabled", "espnow_team_discovery", "espnow_channel", "espnow_beacon_interval_ms", "wifi_enabled", "wifi_sta_enabled", "wifi_ap_enabled", "wifi_persist_credentials", "wifi_sta_channel", "wifi_ap_channel"};
    for (const auto* key : scalar_keys)
        for (const auto failure : {platform::ui::settings_store::ScalarReadResult::Invalid, platform::ui::settings_store::ScalarReadResult::Unavailable})
        {
            failed_scalar = key;
            scalar_failure = failure;
            output.clear();
            assert(!write_c6_records(writer));
            assert(output.empty());
        }
    failed_scalar.clear();
    scalar_failure = platform::ui::settings_store::ScalarReadResult::Ok;
    for (const auto* key : {"espnow_enabled", "espnow_team_discovery", "wifi_enabled", "wifi_sta_enabled", "wifi_ap_enabled", "wifi_persist_credentials"})
    {
        const auto previous = numbers[key];
        numbers[key] = 2;
        output.clear();
        assert(!write_c6_records(writer));
        assert(output.empty());
        numbers[key] = previous;
    }
    numbers.clear();
    output.clear();
    assert(write_c6_records(writer));
    assert(output.find("c6_companion.wifi_ap_channel=u8:1\n") != std::string::npos);
}
