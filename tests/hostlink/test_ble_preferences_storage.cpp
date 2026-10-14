#include "meshtastic/config.pb.h"
#include "meshtastic/localonly.pb.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
struct Action
{
    std::string owner;
    std::string key;
    std::size_t size;
};
std::vector<Action> writes;
std::vector<std::string> opens;
unsigned closes = 0, fail_write = 0, fail_open = 0;
} // namespace

namespace platform::esp::common
{
// The NVS adapter is simulated; compile the production writer and real
// generated BLE/module types so stop/ownership and byte counts are exercised.
class ConfigPreferences
{
    std::string owner_;

  public:
    bool begin(const char* owner, bool)
    {
        opens.emplace_back(owner);
        if (opens.size() == fail_open) return false;
        owner_ = owner;
        return true;
    }
    void end()
    {
        assert(!owner_.empty());
        owner_.clear();
        ++closes;
    }
    std::size_t put(const char* key, std::size_t size)
    {
        assert(!owner_.empty());
        writes.push_back({owner_, key, size});
        return writes.size() == fail_write ? 0 : size;
    }
    std::size_t putBool(const char* key, bool) { return put(key, sizeof(uint8_t)); }
    std::size_t putUChar(const char* key, uint8_t) { return put(key, sizeof(uint8_t)); }
    std::size_t putUInt(const char* key, uint32_t) { return put(key, sizeof(uint32_t)); }
    std::size_t putBytes(const char* key, const void*, std::size_t size) { return put(key, size); }
};
} // namespace platform::esp::common

namespace
{
constexpr const char* kMtBleNamespace = "mt_ble";
constexpr const char* kMcBleNamespace = "mc_ble";
constexpr const char* kMtModuleNamespace = "mt_mod";
constexpr const char* kModuleBlobKey = "cfg";
struct State
{
    meshtastic_Config_BluetoothConfig mt_ble = meshtastic_Config_BluetoothConfig_init_zero;
    uint32_t mc_pin = 0;
    bool mc_manual_add = false;
    uint8_t mc_telemetry_base = 0, mc_telemetry_loc = 0, mc_telemetry_env = 0, mc_advert_location = 0;
    meshtastic_LocalModuleConfig module = meshtastic_LocalModuleConfig_init_zero;
} s_state;
#include "ble_preferences_actual.inc"

void reset()
{
    writes.clear();
    opens.clear();
    closes = fail_write = fail_open = 0;
}
} // namespace

int main()
{
    reset();
    assert(write_preferences());
    assert(writes.size() == 10 && opens.size() == 3 && closes == 3);
    assert(writes.back().owner == "mt_mod" && writes.back().key == "cfg");
    assert(writes.back().size == sizeof(meshtastic_LocalModuleConfig));
    for (unsigned failure = 1; failure <= 10; ++failure)
    {
        reset();
        fail_write = failure;
        assert(!write_preferences());
        assert(writes.size() == failure);
        const unsigned owners = failure <= 3 ? 1 : failure <= 9 ? 2
                                                                : 3;
        assert(opens.size() == owners && closes == owners);
    }
    for (unsigned failure = 1; failure <= 3; ++failure)
    {
        reset();
        fail_open = failure;
        assert(!write_preferences());
        assert(opens.size() == failure && closes == failure - 1);
        assert(writes.size() == (failure == 1 ? 0 : failure == 2 ? 3
                                                                 : 9));
    }
    reset();
    assert(write_preferences());
    assert(writes.size() == 10 && closes == 3);
}
