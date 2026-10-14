#include "platform/esp/arduino_common/app_config_tms_ble_extension.h"

#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "platform/esp/arduino_common/device_identity.h"

#include "platform/esp/common/config_preferences.h"
#include "platform/ui/settings_store.h"
#include <pb_decode.h>
#include <pb_encode.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace app::sd_tms::ble_extension
{
namespace
{

constexpr const char* kMtBleNamespace = "mt_ble";
constexpr const char* kMtModuleNamespace = "mt_mod";
constexpr const char* kMcBleNamespace = "mc_ble";
constexpr const char* kModuleBlobKey = "cfg";
constexpr std::size_t kModuleChunkBytes = 240U;
constexpr std::size_t kModuleChunkCount =
    (meshtastic_LocalModuleConfig_size + kModuleChunkBytes - 1U) / kModuleChunkBytes;
static_assert(kModuleChunkCount == 3U,
              "Update the strict TMS module chunk records when the protobuf limit changes.");

enum Seen : uint32_t
{
    SeenMtEnabled = 1UL << 0U,
    SeenMtMode = 1UL << 1U,
    SeenMtPin = 1UL << 2U,
    SeenMcPin = 1UL << 3U,
    SeenMcManualAdd = 1UL << 4U,
    SeenMcTelemetryBase = 1UL << 5U,
    SeenMcTelemetryLoc = 1UL << 6U,
    SeenMcTelemetryEnv = 1UL << 7U,
    SeenMcAdvertLocation = 1UL << 8U,
    SeenModuleFormat = 1UL << 9U,
    SeenModuleSize = 1UL << 10U,
    SeenModuleChunkCount = 1UL << 11U,
    SeenModuleChunk0 = 1UL << 12U,
    SeenModuleChunk1 = 1UL << 13U,
    SeenModuleChunk2 = 1UL << 14U,
};

constexpr uint32_t kRequired = SeenMtEnabled | SeenMtMode | SeenMtPin | SeenMcPin |
                               SeenMcManualAdd | SeenMcTelemetryBase | SeenMcTelemetryLoc |
                               SeenMcTelemetryEnv | SeenMcAdvertLocation | SeenModuleFormat |
                               SeenModuleSize | SeenModuleChunkCount | SeenModuleChunk0 |
                               SeenModuleChunk1 | SeenModuleChunk2;

struct State
{
    uint32_t seen = 0U;
    meshtastic_Config_BluetoothConfig mt_ble = meshtastic_Config_BluetoothConfig_init_zero;
    uint32_t mc_pin = 0U;
    bool mc_manual_add = false;
    uint8_t mc_telemetry_base = 0U;
    uint8_t mc_telemetry_loc = 0U;
    uint8_t mc_telemetry_env = 0U;
    uint8_t mc_advert_location = 0U;
    char module_format[8] = {};
    uint16_t module_size = 0U;
    uint8_t module_chunk_count = 0U;
    std::size_t module_chunk_lengths[kModuleChunkCount] = {};
    uint8_t module_bytes[meshtastic_LocalModuleConfig_size] = {};
    meshtastic_LocalModuleConfig module = meshtastic_LocalModuleConfig_init_zero;
};

static_assert(sizeof(State) <= 3U * 1024U,
              "TMS BLE staging must remain a bounded transient PSRAM allocation");

State* s_state_storage = nullptr;

#define s_state (*s_state_storage)

bool ensure_state()
{
    if (s_state_storage)
    {
        return true;
    }
#if defined(ESP_PLATFORM)
    void* const raw = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const raw = std::malloc(sizeof(State));
#endif
    s_state_storage = raw ? new (raw) State{} : nullptr;
    return s_state_storage != nullptr;
}

void release_state()
{
    if (!s_state_storage)
    {
        return;
    }
    s_state_storage->~State();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_state_storage);
#else
    std::free(s_state_storage);
#endif
    s_state_storage = nullptr;
}

void initialize_default_module(meshtastic_LocalModuleConfig* module)
{
    if (!module)
    {
        return;
    }
    // Do not call the convenience initializer here: it creates a full
    // LocalModuleConfig automatic temporary. This TMS extension runs before
    // services start and keeps its only module object in PSRAM instead.
    std::memset(module, 0, sizeof(*module));
    const uint32_t self_node =
        ::platform::esp::arduino_common::device_identity::getSelfNodeId();
    module->version = ble::meshtastic_defaults::kModuleConfigVersion;
    module->has_mqtt = true;
    module->has_serial = true;
    module->has_external_notification = true;
    module->has_store_forward = true;
    module->has_range_test = true;
    module->has_telemetry = true;
    module->has_canned_message = true;
    module->has_audio = true;
    module->has_remote_hardware = true;
    module->has_neighbor_info = true;
    module->has_ambient_lighting = true;
    module->has_detection_sensor = true;
    module->has_paxcounter = true;
    std::snprintf(module->mqtt.address,
                  sizeof(module->mqtt.address),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttAddress);
    std::snprintf(module->mqtt.username,
                  sizeof(module->mqtt.username),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttUsername);
    std::snprintf(module->mqtt.password,
                  sizeof(module->mqtt.password),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttPassword);
    std::snprintf(module->mqtt.root,
                  sizeof(module->mqtt.root),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttRoot);
    module->mqtt.enabled = false;
    module->mqtt.proxy_to_client_enabled = false;
    module->mqtt.encryption_enabled = ble::meshtastic_defaults::kDefaultMqttEncryptionEnabled;
    module->mqtt.tls_enabled = ble::meshtastic_defaults::kDefaultMqttTlsEnabled;
    module->mqtt.has_map_report_settings = true;
    module->mqtt.map_report_settings.publish_interval_secs =
        ble::meshtastic_defaults::kDefaultMapPublishIntervalSecs;
    module->mqtt.map_report_settings.position_precision = 0;
    module->mqtt.map_report_settings.should_report_location = false;
    module->telemetry.device_update_interval = 3600;
    module->telemetry.device_telemetry_enabled = true;
    module->telemetry.environment_update_interval = 0;
    module->telemetry.environment_measurement_enabled = false;
    module->telemetry.power_update_interval = 0;
    module->telemetry.health_update_interval = 0;
    module->telemetry.air_quality_interval = 0;
    module->neighbor_info.enabled = false;
    module->neighbor_info.update_interval = 0;
    module->neighbor_info.transmit_over_lora = false;
    module->detection_sensor.enabled = false;
    module->detection_sensor.detection_trigger_type =
        meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH;
    module->detection_sensor.minimum_broadcast_secs =
        ble::meshtastic_defaults::kDefaultDetectionMinBroadcastSecs;
    module->ambient_lighting.current = ble::meshtastic_defaults::kDefaultAmbientCurrent;
    module->ambient_lighting.red = (self_node >> 16U) & 0xFFU;
    module->ambient_lighting.green = (self_node >> 8U) & 0xFFU;
    module->ambient_lighting.blue = self_node & 0xFFU;
#if defined(ROTARY_A) && defined(ROTARY_B) && defined(ROTARY_C)
    module->canned_message.updown1_enabled = true;
    module->canned_message.inputbroker_pin_a = ROTARY_A;
    module->canned_message.inputbroker_pin_b = ROTARY_B;
    module->canned_message.inputbroker_pin_press = ROTARY_C;
    module->canned_message.inputbroker_event_cw =
        meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(28);
    module->canned_message.inputbroker_event_ccw =
        meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(29);
    module->canned_message.inputbroker_event_press =
        meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
#endif
}

void reset_state()
{
    s_state_storage->~State();
    new (s_state_storage) State();
    s_state.mt_ble.enabled = true;
    s_state.mt_ble.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    initialize_default_module(&s_state.module);
}

bool mark_once(uint32_t bit)
{
    if ((s_state.seen & bit) != 0U)
    {
        return false;
    }
    s_state.seen |= bit;
    return true;
}

bool key_equals(const tms::RecordReader& reader, const char* key)
{
    return std::strcmp(reader.key(), key) == 0;
}

bool valid_mc_pin(uint32_t pin)
{
    return pin == 0U || (pin >= 100000U && pin <= 999999U);
}

bool read_mt_mode(const tms::RecordReader& reader)
{
    uint8_t mode = 0U;
    if (!reader.u8(&mode) ||
        !ble::meshtastic_config_bridge::isValidBluetoothMode(mode))
    {
        return false;
    }
    s_state.mt_ble.mode = static_cast<meshtastic_Config_BluetoothConfig_PairingMode>(mode);
    return true;
}

bool decode_module()
{
    if (std::strcmp(s_state.module_format, "pb-v1") != 0 ||
        s_state.module_size == 0U ||
        s_state.module_size > sizeof(s_state.module_bytes) ||
        s_state.module_chunk_count != kModuleChunkCount)
    {
        return false;
    }
    std::size_t expected_offset = 0U;
    for (std::size_t index = 0U; index < kModuleChunkCount; ++index)
    {
        const std::size_t expected =
            s_state.module_size > expected_offset
                ? std::min(kModuleChunkBytes, static_cast<std::size_t>(s_state.module_size) - expected_offset)
                : 0U;
        if (s_state.module_chunk_lengths[index] != expected)
        {
            return false;
        }
        expected_offset += expected;
    }
    std::memset(&s_state.module, 0, sizeof(s_state.module));
    pb_istream_t stream =
        pb_istream_from_buffer(s_state.module_bytes, s_state.module_size);
    if (!pb_decode(&stream, meshtastic_LocalModuleConfig_fields, &s_state.module) ||
        s_state.module.version != ble::meshtastic_defaults::kModuleConfigVersion)
    {
        return false;
    }
    ble::meshtastic_config_bridge::normalizeModuleConfig(&s_state.module);
    return true;
}

bool valid_state()
{
    if ((s_state.seen & kRequired) != kRequired ||
        !ble::meshtastic_config_bridge::isValidBluetoothMode(
            static_cast<uint8_t>(s_state.mt_ble.mode)) ||
        !ble::meshtastic_config_bridge::isValidBlePin(s_state.mt_ble.fixed_pin) ||
        !valid_mc_pin(s_state.mc_pin) || s_state.mc_telemetry_base > 3U ||
        s_state.mc_telemetry_loc > 3U || s_state.mc_telemetry_env > 3U)
    {
        return false;
    }
    if (s_state.mt_ble.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN &&
        s_state.mt_ble.fixed_pin != 0U)
    {
        return false;
    }
    return decode_module();
}

bool load_preferences()
{
    reset_state();
    namespace store = ::platform::ui::settings_store;
    const auto boolean = [](const char* ns, const char* key, bool fallback, bool& out)
    {
        const auto result = store::read_bool_into(ns, key, &out);
        if (result == store::ScalarReadResult::Missing)
        {
            out = fallback;
            return true;
        }
        return result == store::ScalarReadResult::Ok;
    };
    const auto byte = [](const char* ns, const char* key, uint8_t fallback, uint8_t& out)
    {
        const auto result = store::read_u8_into(ns, key, &out);
        if (result == store::ScalarReadResult::Missing)
        {
            out = fallback;
            return true;
        }
        return result == store::ScalarReadResult::Ok;
    };
    const auto integer = [](const char* ns, const char* key, uint32_t fallback, uint32_t& out)
    {
        const auto result = store::read_uint_into(ns, key, &out);
        if (result == store::ScalarReadResult::Missing)
        {
            out = fallback;
            return true;
        }
        return result == store::ScalarReadResult::Ok;
    };
    uint8_t mode = 0;
    if (!boolean(kMtBleNamespace, "enabled", true, s_state.mt_ble.enabled) ||
        !byte(kMtBleNamespace, "mode", static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN), mode) ||
        !integer(kMtBleNamespace, "pin", 0U, s_state.mt_ble.fixed_pin) ||
        !ble::meshtastic_config_bridge::isValidBluetoothMode(mode) ||
        !ble::meshtastic_config_bridge::isValidBlePin(s_state.mt_ble.fixed_pin) ||
        (mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN && s_state.mt_ble.fixed_pin != 0U)) return false;
    s_state.mt_ble.mode = static_cast<meshtastic_Config_BluetoothConfig_PairingMode>(mode);
    if (!integer(kMcBleNamespace, "pin", 0U, s_state.mc_pin) || !valid_mc_pin(s_state.mc_pin) ||
        !boolean(kMcBleNamespace, "manual_add", false, s_state.mc_manual_add) ||
        !byte(kMcBleNamespace, "telem_base", 0U, s_state.mc_telemetry_base) ||
        !byte(kMcBleNamespace, "telem_loc", 0U, s_state.mc_telemetry_loc) ||
        !byte(kMcBleNamespace, "telem_env", 0U, s_state.mc_telemetry_env) ||
        !byte(kMcBleNamespace, "advert_loc", 0U, s_state.mc_advert_location) ||
        s_state.mc_telemetry_base > 3U || s_state.mc_telemetry_loc > 3U || s_state.mc_telemetry_env > 3U) return false;

    // Defaults were staged by reset_state. Preserve them only for an absent
    // module; a failed/truncated/future-version read must not replace user data.
    std::size_t size = 0;
    const auto result = store::read_blob_into(kMtModuleNamespace, kModuleBlobKey,
                                              &s_state.module, sizeof(s_state.module), &size);
    if (result == store::BlobReadResult::Missing) return true;
    if (result != store::BlobReadResult::Ok || size != sizeof(s_state.module) ||
        (s_state.module.version != 0U && s_state.module.version != ble::meshtastic_defaults::kModuleConfigVersion)) return false;
    // Version zero is the explicitly supported legacy form. The existing
    // normalizer upgrades it without discarding unrelated module settings.
    ble::meshtastic_config_bridge::normalizeModuleConfig(&s_state.module);
    return s_state.module.version == ble::meshtastic_defaults::kModuleConfigVersion;
}

bool encode_module()
{
    pb_ostream_t stream = pb_ostream_from_buffer(s_state.module_bytes,
                                                 sizeof(s_state.module_bytes));
    if (!pb_encode(&stream, meshtastic_LocalModuleConfig_fields, &s_state.module) ||
        stream.bytes_written == 0U || stream.bytes_written > sizeof(s_state.module_bytes))
    {
        return false;
    }
    s_state.module_size = static_cast<uint16_t>(stream.bytes_written);
    s_state.module_chunk_count = kModuleChunkCount;
    std::snprintf(s_state.module_format, sizeof(s_state.module_format), "%s", "pb-v1");
    for (std::size_t index = 0U; index < kModuleChunkCount; ++index)
    {
        const std::size_t offset = index * kModuleChunkBytes;
        s_state.module_chunk_lengths[index] =
            s_state.module_size > offset
                ? std::min(kModuleChunkBytes,
                           static_cast<std::size_t>(s_state.module_size) - offset)
                : 0U;
    }
    return true;
}

bool write_preferences()
{
    ::platform::esp::common::ConfigPreferences prefs;
    if (!prefs.begin(kMtBleNamespace, false))
    {
        return false;
    }
    bool ok = prefs.putBool("enabled", s_state.mt_ble.enabled) &&
              prefs.putUChar("mode", static_cast<uint8_t>(s_state.mt_ble.mode)) &&
              prefs.putUInt("pin", s_state.mt_ble.fixed_pin);
    prefs.end();
    if (!ok)
    {
        return false;
    }
    if (!prefs.begin(kMcBleNamespace, false))
    {
        return false;
    }
    ok = prefs.putUInt("pin", s_state.mc_pin) &&
         prefs.putBool("manual_add", s_state.mc_manual_add) &&
         prefs.putUChar("telem_base", s_state.mc_telemetry_base) &&
         prefs.putUChar("telem_loc", s_state.mc_telemetry_loc) &&
         prefs.putUChar("telem_env", s_state.mc_telemetry_env) &&
         prefs.putUChar("advert_loc", s_state.mc_advert_location);
    prefs.end();
    if (!ok)
    {
        return false;
    }
    if (!prefs.begin(kMtModuleNamespace, false))
    {
        return false;
    }
    ok = prefs.putBytes(kModuleBlobKey, &s_state.module, sizeof(s_state.module)) ==
         sizeof(s_state.module);
    prefs.end();
    return ok;
}

} // namespace

bool beginRead()
{
    if (!ensure_state())
    {
        return false;
    }
    reset_state();
    return true;
}

void endRead()
{
    release_state();
}

tms::RecordConsumeResult consumeRecord(const tms::RecordReader& reader)
{
    if (!s_state_storage)
    {
        return tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mt_ble.enabled"))
    {
        return mark_once(SeenMtEnabled) && reader.boolean(&s_state.mt_ble.enabled)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mt_ble.mode"))
    {
        return mark_once(SeenMtMode) && read_mt_mode(reader)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mt_ble.fixed_pin"))
    {
        return mark_once(SeenMtPin) && reader.u32(&s_state.mt_ble.fixed_pin)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mc_ble.pin"))
    {
        return mark_once(SeenMcPin) && reader.u32(&s_state.mc_pin)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mc_ble.manual_add_contacts"))
    {
        return mark_once(SeenMcManualAdd) && reader.boolean(&s_state.mc_manual_add)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mc_ble.telemetry_base"))
    {
        return mark_once(SeenMcTelemetryBase) && reader.u8(&s_state.mc_telemetry_base, 3U)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mc_ble.telemetry_location"))
    {
        return mark_once(SeenMcTelemetryLoc) && reader.u8(&s_state.mc_telemetry_loc, 3U)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mc_ble.telemetry_environment"))
    {
        return mark_once(SeenMcTelemetryEnv) && reader.u8(&s_state.mc_telemetry_env, 3U)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mc_ble.advert_location_policy"))
    {
        return mark_once(SeenMcAdvertLocation) && reader.u8(&s_state.mc_advert_location)
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mt_module.format"))
    {
        return mark_once(SeenModuleFormat) &&
                       reader.text(s_state.module_format, sizeof(s_state.module_format))
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mt_module.size"))
    {
        return mark_once(SeenModuleSize) &&
                       reader.u16(&s_state.module_size,
                                  static_cast<uint16_t>(sizeof(s_state.module_bytes)))
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    if (key_equals(reader, "mt_module.chunk_count"))
    {
        return mark_once(SeenModuleChunkCount) &&
                       reader.u8(&s_state.module_chunk_count,
                                 static_cast<uint8_t>(kModuleChunkCount))
                   ? tms::RecordConsumeResult::Accepted
                   : tms::RecordConsumeResult::Invalid;
    }
    for (std::size_t index = 0U; index < kModuleChunkCount; ++index)
    {
        char key[32]{};
        std::snprintf(key, sizeof(key), "mt_module.chunk.%u", static_cast<unsigned>(index));
        if (key_equals(reader, key))
        {
            const uint32_t bit = SeenModuleChunk0 << index;
            return mark_once(bit) &&
                           reader.blob(s_state.module_bytes + index * kModuleChunkBytes,
                                       kModuleChunkBytes,
                                       &s_state.module_chunk_lengths[index])
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
    }
    return tms::RecordConsumeResult::Unhandled;
}

bool finishDocument(bool applying, uint16_t schema_version)
{
    if (!s_state_storage)
    {
        return false;
    }
    if (s_state.seen == 0U)
    {
        return schema_version != tms::kSchemaVersion;
    }
    if (!valid_state())
    {
        return false;
    }
    return !applying || write_preferences();
}

bool writeRecords(tms::RecordWriter& writer)
{
    if (!beginRead() || !load_preferences() || !encode_module())
    {
        endRead();
        return false;
    }
    bool ok = writer.boolean("mt_ble.enabled", s_state.mt_ble.enabled) &&
              writer.u8("mt_ble.mode", static_cast<uint8_t>(s_state.mt_ble.mode)) &&
              writer.u32("mt_ble.fixed_pin", s_state.mt_ble.fixed_pin) &&
              writer.u32("mc_ble.pin", s_state.mc_pin) &&
              writer.boolean("mc_ble.manual_add_contacts", s_state.mc_manual_add) &&
              writer.u8("mc_ble.telemetry_base", s_state.mc_telemetry_base) &&
              writer.u8("mc_ble.telemetry_location", s_state.mc_telemetry_loc) &&
              writer.u8("mc_ble.telemetry_environment", s_state.mc_telemetry_env) &&
              writer.u8("mc_ble.advert_location_policy", s_state.mc_advert_location) &&
              writer.text("mt_module.format", s_state.module_format) &&
              writer.u16("mt_module.size", s_state.module_size) &&
              writer.u8("mt_module.chunk_count", s_state.module_chunk_count);
    for (std::size_t index = 0U; ok && index < kModuleChunkCount; ++index)
    {
        char key[32]{};
        std::snprintf(key, sizeof(key), "mt_module.chunk.%u", static_cast<unsigned>(index));
        ok = writer.blob(key,
                         s_state.module_bytes + index * kModuleChunkBytes,
                         s_state.module_chunk_lengths[index]);
    }
    endRead();
    return ok;
}

#undef s_state

} // namespace app::sd_tms::ble_extension
