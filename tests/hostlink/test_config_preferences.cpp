#include "platform/esp/common/config_preferences.h"
#include <cassert>

int main()
{
    using platform::esp::common::ConfigPreferences;
    static_assert(sizeof(ConfigPreferences) <= 8, "NVS adapter must contain only handle and mode");
    {
        ConfigPreferences preferences;
        assert(!preferences.begin("missing", true));
        assert(preferences.getUInt("pin", 123) == 123);
        assert(!preferences.putUInt("pin", 42));
        assert(preferences.begin("mt_ble", false));
        assert(preferences.putBool("enabled", false) == 1);
        assert(preferences.putUChar("mode", 0) == 1);
        assert(preferences.putUInt("pin", 0) == sizeof(uint32_t));
        // Verify exact legacy Arduino NVS scalar representations, including zero.
        assert(std::get<uint8_t>(fake_nvs::values["mt_ble"]["enabled"]) == 0);
        assert(std::get<uint8_t>(fake_nvs::values["mt_ble"]["mode"]) == 0);
        assert(std::get<uint32_t>(fake_nvs::values["mt_ble"]["pin"]) == 0);
        const uint8_t module[] = {0, 1, 2, 3};
        assert(preferences.putBytes("cfg", module, sizeof(module)) == sizeof(module));
        uint8_t buffer[4];
        assert(preferences.getBytes("cfg", buffer, 3) == 0);
        assert(preferences.getBytes("cfg", buffer, sizeof(buffer)) == sizeof(module));
        assert(!std::memcmp(buffer, module, sizeof(module)));
        assert(preferences.begin("mt_ble", true));
        assert(fake_nvs::handles.size() == 1);
        assert(!preferences.getBool("enabled", true));
        assert(preferences.getUInt("pin", 123) == 0);
        assert(!preferences.putBool("enabled", true));
        assert(!preferences.remove("pin"));
        assert(preferences.begin("cfgreset", false));
        assert(preferences.remove("working"));
        assert(preferences.putBool("working", true));
        fake_nvs::fail_commit = true;
        assert(!preferences.putBool("working", false));
        fake_nvs::fail_commit = false;
        assert(preferences.remove("working"));
        assert(!preferences.getBool("working"));
    }
    assert(fake_nvs::handles.empty());
}
