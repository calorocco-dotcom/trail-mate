#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "platform/ui/settings_store.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <string>

namespace
{
using Result = platform::ui::settings_store::ScalarReadResult;
std::map<std::string, uint32_t> values;
std::string failed;
Result failure = Result::Ok;
platform::ui::settings_store::BlobReadResult blob_result = platform::ui::settings_store::BlobReadResult::Missing;
std::size_t module_size = 0;
meshtastic_LocalModuleConfig stored_module = meshtastic_LocalModuleConfig_init_zero;
unsigned default_initializations = 0, normalizations = 0;
std::string key_for(const char* ns, const char* key) { return std::string(ns) + "/" + key; }
Result fetch(const char* ns, const char* key, uint32_t& value)
{
    value = 0;
    const auto id = key_for(ns, key);
    if (id == failed) return failure;
    const auto found = values.find(id);
    if (found == values.end()) return Result::Missing;
    value = found->second;
    return Result::Ok;
}
} // namespace
namespace platform::ui::settings_store
{
ScalarReadResult read_uint_into(const char* ns, const char* key, uint32_t* out) { return fetch(ns, key, *out); }
ScalarReadResult read_u8_into(const char* ns, const char* key, uint8_t* out)
{
    uint32_t value;
    auto result = fetch(ns, key, value);
    *out = 0;
    if (result != Result::Ok) return result;
    if (value > 255) return Result::Invalid;
    *out = static_cast<uint8_t>(value);
    return Result::Ok;
}
ScalarReadResult read_bool_into(const char* ns, const char* key, bool* out)
{
    uint32_t value;
    auto result = fetch(ns, key, value);
    *out = false;
    if (result != Result::Ok) return result;
    if (value > 1) return Result::Invalid;
    *out = value != 0;
    return Result::Ok;
}
BlobReadResult read_blob_into(const char*, const char*, void* out, std::size_t capacity, std::size_t* length)
{
    *length = 0;
    if (blob_result != BlobReadResult::Ok) return blob_result;
    if (module_size > capacity) return BlobReadResult::Invalid;
    std::memcpy(out, &stored_module, module_size);
    *length = module_size;
    return BlobReadResult::Ok;
}
} // namespace platform::ui::settings_store
namespace platform::esp::common
{
class ConfigPreferences
{
    const char* owner_ = nullptr;

  public:
    bool begin(const char* owner, bool)
    {
        owner_ = owner;
        return true;
    }
    void end() { owner_ = nullptr; }
    uint32_t getUInt(const char* key, uint32_t fallback = 0)
    {
        uint32_t value;
        return fetch(owner_, key, value) == Result::Ok ? value : fallback;
    }
    uint8_t getUChar(const char* key, uint8_t fallback = 0) { return static_cast<uint8_t>(getUInt(key, fallback)); }
    bool getBool(const char* key, bool fallback = false) { return getUInt(key, fallback) != 0; }
    std::size_t getBytes(const char*, void* out, std::size_t capacity)
    {
        std::size_t size = 0;
        return ::platform::ui::settings_store::read_blob_into(owner_, "cfg", out, capacity, &size) == ::platform::ui::settings_store::BlobReadResult::Ok ? size : 0;
    }
};
} // namespace platform::esp::common
namespace platform::esp::arduino_common::device_identity
{
uint32_t getSelfNodeId()
{
    ++default_initializations;
    return 0x102030;
}
} // namespace platform::esp::arduino_common::device_identity
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
} state;
State* s_state_storage = &state;
#define s_state (*s_state_storage)
#include "ble_defaults_actual.inc"

bool valid_mc_pin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000 && pin <= 999999);
}
#include "ble_load_actual.inc"
#undef s_state
void reset_fixture()
{
    values = {{"mt_ble/enabled", 0}, {"mt_ble/mode", 1}, {"mt_ble/pin", 123456}, {"mc_ble/pin", 654321}, {"mc_ble/manual_add", 0}, {"mc_ble/telem_base", 1}, {"mc_ble/telem_loc", 2}, {"mc_ble/telem_env", 3}, {"mc_ble/advert_loc", 255}};
    failed.clear();
    failure = Result::Ok;
    blob_result = platform::ui::settings_store::BlobReadResult::Ok;
    module_size = sizeof(stored_module);
    std::memset(&stored_module, 0, sizeof(stored_module));
    stored_module.version = ble::meshtastic_defaults::kModuleConfigVersion;
    stored_module.has_serial = true;
    stored_module.serial.enabled = true;
    stored_module.serial.baud = meshtastic_ModuleConfig_SerialConfig_Serial_Baud_BAUD_9600;
    default_initializations = 0;
}
} // namespace
int main()
{
    reset_fixture();
    assert(load_preferences());
    assert(!state.mt_ble.enabled && state.mt_ble.fixed_pin == 123456 && state.mc_pin == 654321);
    assert(state.mc_telemetry_env == 3 && state.mc_advert_location == 255 && state.module.serial.enabled);
    const auto baseline = values;
    for (const auto& item : baseline)
        for (auto result : {Result::Invalid, Result::Unavailable})
        {
            reset_fixture();
            failed = item.first;
            failure = result;
            assert(!load_preferences());
        }
    for (auto result : {platform::ui::settings_store::BlobReadResult::Invalid,
                        platform::ui::settings_store::BlobReadResult::Unavailable})
    {
        reset_fixture();
        blob_result = result;
        assert(!load_preferences());
        assert(default_initializations == 1); // No replacement after failed read.
    }
    for (auto size : {std::size_t(0), sizeof(stored_module) - 1, sizeof(stored_module) + 1})
    {
        reset_fixture();
        module_size = size;
        assert(!load_preferences());
    }
    reset_fixture();
    stored_module.version = ble::meshtastic_defaults::kModuleConfigVersion + 1;
    assert(!load_preferences());
    reset_fixture();
    stored_module.version = 0; // Explicitly supported legacy normalization.
    assert(load_preferences());
    assert(state.module.version == ble::meshtastic_defaults::kModuleConfigVersion && state.module.serial.enabled);
    for (const auto& item : std::map<std::string, uint32_t>{{"mt_ble/mode", 255}, {"mt_ble/pin", 1}, {"mc_ble/pin", 999}, {"mc_ble/telem_base", 4}, {"mc_ble/telem_loc", 255}, {"mc_ble/telem_env", 4}})
    {
        reset_fixture();
        values[item.first] = item.second;
        assert(!load_preferences());
    }
    reset_fixture();
    values["mt_ble/mode"] = meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    assert(!load_preferences());
    reset_fixture();
    values.clear();
    blob_result = platform::ui::settings_store::BlobReadResult::Missing;
    assert(load_preferences());
    assert(state.mt_ble.enabled && state.mt_ble.fixed_pin == 0 && state.mc_pin == 0);
    assert(default_initializations == 1);
}
