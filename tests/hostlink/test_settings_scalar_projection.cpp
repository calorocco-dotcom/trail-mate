#include "app/tms_config_codec.h"
#include "platform/ui/auto_reply_settings.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/timezone_profile.h"
#include "platform/ui/wifi_runtime.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>

namespace platform::ui::settings_store
{
std::map<std::string, int64_t> values = {
    {"screen_timeout", 300000}, {"screen_brightness", 15}, {"speaker_volume", 0}, {"vibration_enabled", 0}, {"timezone_profile", 11}, {"timezone_offset", 60}, {"chat_message_alerts", 0}, {"chat_contact_alerts", 2}, {"chat_auto_reply_enabled", 0}, {"adv_debug", 0}, {"gauge_design_mah", 1500}, {"gauge_full_mah", 1400}};
std::vector<std::string> reads;
std::string failed_key;
ScalarReadResult failure = ScalarReadResult::Ok;
ScalarReadResult fetch(const char* key, int64_t& value)
{
    reads.emplace_back(key);
    value = 0;
    if (failed_key == key) return failure;
    const auto found = values.find(key);
    if (found == values.end()) return ScalarReadResult::Missing;
    value = found->second;
    return ScalarReadResult::Ok;
}
ScalarReadResult read_int_into(const char*, const char* key, int32_t* out)
{
    int64_t value;
    auto result = fetch(key, value);
    *out = static_cast<int32_t>(value);
    return result;
}
ScalarReadResult read_uint_into(const char*, const char* key, uint32_t* out)
{
    int64_t value;
    auto result = fetch(key, value);
    *out = static_cast<uint32_t>(value);
    return result;
}
ScalarReadResult read_bool_into(const char*, const char* key, bool* out)
{
    int64_t value;
    auto result = fetch(key, value);
    *out = value != 0;
    return result;
}
int get_int(const char* ns, const char* key, int fallback)
{
    int32_t value;
    return read_int_into(ns, key, &value) == ScalarReadResult::Ok ? value : fallback;
}
uint32_t get_uint(const char* ns, const char* key, uint32_t fallback)
{
    uint32_t value;
    return read_uint_into(ns, key, &value) == ScalarReadResult::Ok ? value : fallback;
}
bool get_bool(const char* ns, const char* key, bool fallback)
{
    bool value;
    return read_bool_into(ns, key, &value) == ScalarReadResult::Ok ? value : fallback;
}
} // namespace platform::ui::settings_store
namespace platform::ui::screen
{
uint32_t timeout_ms() { return 30000; }
} // namespace platform::ui::screen
namespace platform::ui::device
{
uint8_t screen_brightness() { return 8; }
} // namespace platform::ui::device
namespace platform::ui::time
{
std::time_t fixture_epoch = 1782907200; // 2026-07-01T12:00:00Z
int timezone_profile_id() { return 11; }
int timezone_offset_min() { return timezone_offset_for_profile_id_at(11, 60, fixture_epoch); }
} // namespace platform::ui::time
namespace platform::ui::wifi
{
bool snapshot_available = true;
bool read_saved_profiles_into(bool* enabled, std::size_t* count, Config*, std::size_t)
{
    *enabled = false;
    *count = 0;
    return snapshot_available;
}
} // namespace platform::ui::wifi
namespace
{
namespace tms = app::tms;
using app::AppConfig;
constexpr const char* kSettingsNs = "settings";
constexpr const char* kPowerNs = "power";
constexpr std::size_t kLocaleBytes = 48, kEnabledImesBytes = 192, kAutoReplyBytes = 171;
struct State
{
    uint8_t timezone_tzdef[192]{};
    platform::ui::wifi::Config wifi_profiles[10]{};
} s_state;
class ScopedWriteState
{
  public:
    bool ready() const { return true; }
};
unsigned later_owners = 0;
bool write_text_setting(tms::RecordWriter&, const char*, const char*, const char*, char*, std::size_t) { return true; }
bool write_string_as_blob(tms::RecordWriter&, const char*, const char*, const char*, std::size_t) { return true; }
bool write_blob_setting(tms::RecordWriter&, const char*, const char*, const char*) { return true; }
bool write_network_records(tms::RecordWriter&, const AppConfig&)
{
    ++later_owners;
    return true;
}
namespace ble_extension
{
bool writeRecords(tms::RecordWriter&)
{
    ++later_owners;
    return true;
}
} // namespace ble_extension
#include "settings_scalar_projection_actual.inc"

#include "settings_records_actual.inc"
bool append(void* context, const char* bytes, std::size_t size)
{
    static_cast<std::string*>(context)->append(bytes, size);
    return true;
}
} // namespace
int main()
{
    namespace store = platform::ui::settings_store;
    app::AppConfig config;
    std::string text;
    tms::LineScratch scratch{};
    tms::RecordWriter writer({&text, append}, scratch, nullptr);
    assert(platform::ui::time::timezone_offset_min() == 120);
    assert(writeRecords(&config, writer));
    assert(text.find("ui.timezone.offset_minutes=i32:60\n") != std::string::npos);
    assert(text.find("ui.screen.timeout_ms=u32:300000\n") != std::string::npos);
    assert(text.find("ui.audio.speaker_volume=i32:0\n") != std::string::npos);
    assert(text.find("radio.enabled=bool:1\n") != std::string::npos);
    assert(text.find("ui.haptics.vibration_enabled=bool:0\n") != std::string::npos);
    assert(store::reads.size() == 13 && later_owners == 2);
    const auto keys = store::reads;
    for (const auto& key : keys)
    {
        for (auto failure : {store::ScalarReadResult::Missing, store::ScalarReadResult::Invalid,
                             store::ScalarReadResult::Unavailable})
        {
            store::failed_key = key;
            store::failure = failure;
            store::reads.clear();
            later_owners = 0;
            text.clear();
            assert(writeRecords(&config, writer) == (failure == store::ScalarReadResult::Missing));
            assert(later_owners == (failure == store::ScalarReadResult::Missing ? 2 : 0));
            if (failure != store::ScalarReadResult::Missing) assert(store::reads.back() == key);
        }
    }
    store::failed_key.clear();
    store::failure = store::ScalarReadResult::Ok;
    text.clear();
    platform::ui::time::fixture_epoch = 1767268800; // 2026-01-01T12:00:00Z
    assert(platform::ui::time::timezone_offset_min() == 60);
    assert(writeRecords(&config, writer));
    // Date-derived runtime offsets can change while persisted settings remain
    // unchanged. Repeated snapshots must retain the saved fixed-offset input.
    assert(text.find("ui.timezone.offset_minutes=i32:60\n") != std::string::npos);
    text.clear();
    later_owners = 0;
    platform::ui::wifi::snapshot_available = false;
    assert(!writeRecords(&config, writer));
    assert(later_owners == 1); // Network emitted; BLE must not run after failure.
    assert(text.find("wifi.profile_count=") == std::string::npos);
}
