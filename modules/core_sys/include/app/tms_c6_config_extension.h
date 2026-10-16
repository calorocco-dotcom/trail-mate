#pragma once
#include "app/tms_config_codec.h"
#include <cstring>

namespace app::tms::c6
{
// Caller-owned decode staging. ESP callers must place this in their existing
// on-demand PSRAM owner, never an automatic task-stack object.
struct Config
{
    uint16_t seen = 0;
    bool espnow_enabled = false, espnow_team_discovery = true;
    uint8_t espnow_channel = 0;
    uint16_t espnow_beacon_interval_ms = 1000;
    bool wifi_enabled = false, wifi_sta_enabled = false, wifi_ap_enabled = false, wifi_persist_credentials = false;
    char wifi_sta_ssid[32]{}, wifi_sta_password[64]{}, wifi_ap_ssid[32] = "TrailMate-C6", wifi_ap_password[64]{};
    uint8_t wifi_sta_channel = 0, wifi_ap_channel = 1;
};
constexpr uint16_t kComplete = (1U << 14U) - 1U;
inline bool valid(const Config& config, bool shared_wifi_enabled = false)
{
    const bool wifi_enabled = config.wifi_enabled || (shared_wifi_enabled && config.wifi_sta_ssid[0]);
    return config.seen == kComplete && config.espnow_channel <= 14 && config.wifi_sta_channel <= 14 &&
           config.wifi_ap_channel >= 1 && config.wifi_ap_channel <= 14 &&
           config.espnow_beacon_interval_ms >= 1 && config.espnow_beacon_interval_ms <= 60000 &&
           std::memchr(config.wifi_sta_ssid, 0, sizeof(config.wifi_sta_ssid)) &&
           std::memchr(config.wifi_sta_password, 0, sizeof(config.wifi_sta_password)) &&
           std::memchr(config.wifi_ap_ssid, 0, sizeof(config.wifi_ap_ssid)) &&
           std::memchr(config.wifi_ap_password, 0, sizeof(config.wifi_ap_password)) &&
           (!wifi_enabled || !config.wifi_sta_enabled || config.wifi_sta_ssid[0]) &&
           (!wifi_enabled || !config.wifi_ap_enabled || config.wifi_ap_ssid[0]) &&
           (!wifi_enabled || !config.wifi_ap_enabled || !config.wifi_ap_password[0] || std::strlen(config.wifi_ap_password) >= 8);
}
inline RecordConsumeResult consume(Config& config, const RecordReader& reader)
{
    const char* key = reader.key();
    if (!key || std::strncmp(key, "c6_companion.", 13)) return RecordConsumeResult::Unhandled;
    key += 13;
    unsigned index = 0;
    bool ok = false;
#define C6_FIELD(name, method, number)                                        \
    if (!std::strcmp(key, #name))                                             \
    {                                                                         \
        index = number;                                                       \
        if (config.seen & (1U << index)) return RecordConsumeResult::Invalid; \
        ok = reader.method(&config.name);                                     \
    }
#define C6_TEXT(name, number)                                                 \
    if (!std::strcmp(key, #name))                                             \
    {                                                                         \
        index = number;                                                       \
        if (config.seen & (1U << index)) return RecordConsumeResult::Invalid; \
        ok = reader.text(config.name, sizeof(config.name));                   \
    }
    C6_FIELD(espnow_enabled, boolean, 0)
    else C6_FIELD(espnow_team_discovery, boolean, 1) else C6_FIELD(espnow_channel, u8, 2) else C6_FIELD(espnow_beacon_interval_ms, u16, 3) else C6_FIELD(wifi_enabled, boolean, 4) else C6_FIELD(wifi_sta_enabled, boolean, 5) else C6_FIELD(wifi_ap_enabled, boolean, 6) else C6_FIELD(wifi_persist_credentials, boolean, 7) else C6_TEXT(wifi_sta_ssid, 8) else C6_TEXT(wifi_sta_password, 9) else C6_TEXT(wifi_ap_ssid, 10) else C6_TEXT(wifi_ap_password, 11) else C6_FIELD(wifi_sta_channel, u8, 12) else C6_FIELD(wifi_ap_channel, u8, 13) else return RecordConsumeResult::Invalid;
#undef C6_FIELD
#undef C6_TEXT
    if (!ok) return RecordConsumeResult::Invalid;
    config.seen |= uint16_t(1U << index);
    return RecordConsumeResult::Accepted;
}
inline bool write(const Config& config, RecordWriter& writer)
{
    if (!valid(config)) return false;
#define C6_WRITE(name, method) writer.method("c6_companion." #name, config.name)
    return C6_WRITE(espnow_enabled, boolean) && C6_WRITE(espnow_team_discovery, boolean) &&
           C6_WRITE(espnow_channel, u8) && C6_WRITE(espnow_beacon_interval_ms, u16) &&
           C6_WRITE(wifi_enabled, boolean) && C6_WRITE(wifi_sta_enabled, boolean) &&
           C6_WRITE(wifi_ap_enabled, boolean) && C6_WRITE(wifi_persist_credentials, boolean) &&
           C6_WRITE(wifi_sta_ssid, text) && C6_WRITE(wifi_sta_password, text) &&
           C6_WRITE(wifi_ap_ssid, text) && C6_WRITE(wifi_ap_password, text) &&
           C6_WRITE(wifi_sta_channel, u8) && C6_WRITE(wifi_ap_channel, u8);
#undef C6_WRITE
}
} // namespace app::tms::c6
