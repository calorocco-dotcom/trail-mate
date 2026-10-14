#include "platform/esp/common/wifi_profile_snapshot.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>

namespace platform::ui::settings_store
{
std::map<std::string, int32_t> scalars;
std::map<std::string, std::string> texts;
std::string fault_key;
bool unavailable = false;
std::vector<std::string> reads;
ScalarReadResult read_int_into(const char*, const char* key, int32_t* out)
{
    reads.emplace_back(key);
    *out = 0;
    if (fault_key == key) return unavailable ? ScalarReadResult::Unavailable : ScalarReadResult::Invalid;
    const auto entry = scalars.find(key);
    if (entry == scalars.end()) return ScalarReadResult::Missing;
    *out = entry->second;
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
    return result;
}
StringReadResult read_string_into(const char*, const char* key, char* out,
                                  std::size_t capacity, std::size_t* size)
{
    reads.emplace_back(key);
    *size = 0;
    std::memset(out, 0, capacity);
    if (fault_key == key) return unavailable ? StringReadResult::Unavailable : StringReadResult::Invalid;
    const auto entry = texts.find(key);
    if (entry == texts.end()) return StringReadResult::Missing;
    if (entry->second.size() >= capacity) return StringReadResult::Invalid;
    std::memcpy(out, entry->second.c_str(), entry->second.size() + 1);
    *size = entry->second.size();
    return StringReadResult::Ok;
}
} // namespace platform::ui::settings_store

namespace platform::ui::wifi
{
#if defined(WIFI_SNAPSHOT_IDF)
#include "wifi_idf_snapshot_actual.inc"
#else
#include "wifi_common_snapshot_actual.inc"
#endif
} // namespace platform::ui::wifi

int main()
{
    namespace store = platform::ui::settings_store;
    using platform::ui::wifi::Config;
    constexpr auto capacity = platform::ui::wifi::kMaxSavedProfileCount;
    Config profiles[capacity]{}; // Desktop fixture; firmware supplies PSRAM staging.
    bool enabled = true;
    std::size_t count = 99;
    const auto read = [&]
    { return platform::ui::wifi::read_saved_profiles_into(&enabled, &count, profiles, capacity); };
    const auto empty = [&]
    {
        assert(!enabled && count == 0);
        for (const auto& profile : profiles)
            assert(!profile.enabled && profile.ssid[0] == '\0' && profile.password[0] == '\0');
    };
    assert(read()); // Fresh NVS: default disabled, no profiles.
    empty();
    store::scalars = {{"wifi_enabled", 1}, {"wifi_prof_count", 10}};
    for (std::size_t i = 0; i < capacity; ++i)
    {
        store::texts["wifi_ssid_" + std::to_string(i)] = "fixture-" + std::to_string(i);
        store::texts["wifi_password_" + std::to_string(i)] = std::string(i == 0 ? 64 : 8, 'x');
    }
    store::texts["wifi_ssid"] = "fixture-0";
    store::texts["wifi_password"] = "old-mirror";
    store::reads.clear();
    assert(read());
    assert(enabled && count == 10 && std::strlen(profiles[0].password) == 64);
    for (std::size_t i = 0; i < capacity; ++i)
        assert(profiles[i].ssid == std::string("fixture-") + std::to_string(i));
    const auto keys = store::reads;
    assert(keys.size() == 24);
    for (const auto& key : keys)
    {
        for (const bool unavailable : {false, true})
        {
            store::fault_key = key;
            store::unavailable = unavailable;
            assert(!read());
            empty(); // No partial profile set or secret retained on error.
        }
    }
    store::fault_key.clear();
    const auto original = store::texts;
    for (std::size_t i = 0; i < capacity; ++i)
    {
        const auto ssid_key = "wifi_ssid_" + std::to_string(i);
        store::texts.erase(ssid_key);
        assert(!read()); // Explicit count cannot silently shrink.
        empty();
        store::texts = original;
    }
    store::texts["wifi_ssid_1"] = store::texts["wifi_ssid_0"];
    assert(!read()); // Malformed explicit set: duplicate SSID.
    store::texts = original;
    for (const auto value : {-1, 11})
    {
        store::scalars["wifi_prof_count"] = value;
        assert(!read());
        empty();
    }
    store::scalars["wifi_prof_count"] = 10;
    for (const auto value : {2, 255})
    {
        store::scalars["wifi_enabled"] = value;
        assert(!read());
    }
    store::scalars["wifi_enabled"] = 1;
    store::texts["wifi_ssid_0"] = std::string(33, 's');
    assert(!read()); // Never truncate an SSID into a different network.
    store::texts = original;
    store::texts["wifi_password_0"] = std::string(65, 'p');
    assert(!read());
    store::texts = original;

    // Current empty password has priority over an obsolete legacy password.
    store::texts["wifi_password_0"] = "";
    store::texts["wifi_pass_0"] = "legacy-fixture";
    assert(read() && profiles[0].password[0] == '\0');
    store::texts.erase("wifi_password_0");
    assert(read() && !std::strcmp(profiles[0].password, "legacy-fixture"));
    for (const bool unavailable : {false, true})
    {
        store::fault_key = "wifi_pass_0";
        store::unavailable = unavailable;
        assert(!read());
        empty();
    }
    store::fault_key.clear();
    store::texts.erase("wifi_pass_0");
    assert(read() && profiles[0].password[0] == '\0'); // Open network.

    // Missing/zero count recovers sparse legacy slots without a storage write.
    store::scalars.erase("wifi_prof_count");
    store::texts = {{"wifi_ssid_4", "legacy-network"}, {"wifi_pass_4", "legacy-fixture"}, {"wifi_ssid_7", "legacy-network"}, {"wifi_password_7", "second-duplicate"}, {"wifi_ssid", "primary-network"}, {"wifi_password", "primary-fixture"}};
    const auto legacy = store::texts;
    assert(read() && count == 2);
    assert(!std::strcmp(profiles[0].password, "legacy-fixture"));
    assert(!std::strcmp(profiles[1].ssid, "primary-network"));
    assert(store::texts == legacy && !store::scalars.count("wifi_prof_count"));
    store::scalars["wifi_prof_count"] = 0;
    assert(read() && count == 2);
    store::texts.clear();
    store::texts["wifi_ssid"] = std::string(32, 's');
    store::texts["wifi_password"] = std::string(64, 'p');
    assert(read() && count == 1 && std::strlen(profiles[0].ssid) == 32);
    store::texts = original;
    store::scalars["wifi_prof_count"] = 10;
    store::texts["wifi_ssid"] = "overflow-primary";
    assert(!read()); // Cannot silently drop the eleventh unique network.
    empty();

    store::reads.clear();
    assert(!platform::esp::common::read_persisted_wifi_profiles(nullptr, &count, profiles, capacity));
    assert(!platform::esp::common::read_persisted_wifi_profiles(&enabled, nullptr, profiles, capacity));
    assert(!platform::esp::common::read_persisted_wifi_profiles(&enabled, &count, nullptr, capacity));
    for (auto& profile : profiles) profile.ssid[0] = 'x';
    assert(!platform::esp::common::read_persisted_wifi_profiles(&enabled, &count, profiles, capacity - 1));
    for (std::size_t i = 0; i < capacity - 1; ++i) assert(profiles[i].ssid[0] == '\0');
    assert(profiles[capacity - 1].ssid[0] == 'x'); // Guard never touches past capacity.
    assert(store::reads.empty());
}
