#include "platform/memory/psram_ptr.h"
#include "platform/ui/wifi_runtime.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace platform::ui::wifi;
constexpr uint16_t kWifiAutoScanMaxRecords = 16;
using esp_err_t = int;
enum class WifiInitProfile
{
    Lightweight,
    EmergencyLowMemory
};
struct wifi_init_config_t
{
    int nvs_enable = 1, tx_buf_type = 0, static_tx_buf_num = 8, static_rx_buf_num = 8;
    int rx_ba_win = 16, dynamic_rx_buf_num = 32, dynamic_tx_buf_num = 32;
    int cache_tx_buf_num = 16, mgmt_sbuf_num = 32, rx_mgmt_buf_num = 10;
    int ampdu_rx_enable = 1, ampdu_tx_enable = 1, amsdu_tx_enable = 0;
};
#include "wifi_limits.inc"
#include "wifi_profile.inc"
struct wifi_ap_record_t
{
    char ssid[33]{};
};
struct wifi_scan_config_t
{
};
constexpr int ESP_OK = 0;
bool enabled = true, started = true;
int scan_error = 0, count_error = 0, records_error = 0;
uint16_t ap_count = 4;
struct Runtime
{
    bool scanning = false;
} s_runtime;
char message[128]{};
bool platform::ui::wifi::load_config(Config& out)
{
    out.enabled = enabled;
    return true;
}
void set_status_message(const char* text) { std::strcpy(message, text); }
bool ensure_wifi_started() { return started; }
void refresh_runtime_status_message() {}
int esp_wifi_scan_start(wifi_scan_config_t*, bool)
{
    assert(live_bytes);
    return scan_error;
}
int esp_wifi_scan_get_ap_num(uint16_t* count)
{
    *count = ap_count;
    return count_error;
}
int esp_wifi_scan_get_ap_records(uint16_t* count, wifi_ap_record_t* out)
{
    assert(live_bytes == sizeof(wifi_ap_record_t) * 16);
    for (uint16_t i = 0; i < *count; ++i) std::strcpy(out[i].ssid, "test");
    return records_error;
}
void insert_scan_result_sorted(ScanResult*, size_t, size_t& count, const wifi_ap_record_t&) { ++count; }
namespace actual
{
#include "wifi_scan.inc"
}
int main()
{
    wifi_init_config_t config;
    apply_wifi_init_profile(config, WifiInitProfile::Lightweight);
    assert(config.nvs_enable == 0 && config.rx_ba_win == 4 && config.static_rx_buf_num == 4);
    assert(config.dynamic_rx_buf_num == 16 && config.dynamic_tx_buf_num == 16);
    apply_wifi_init_profile(config, WifiInitProfile::EmergencyLowMemory);
    assert(config.ampdu_rx_enable == 0 && config.rx_ba_win == 0);
    assert(config.dynamic_rx_buf_num == 8 && config.dynamic_tx_buf_num == 8);
    ScanResult results[16]{};
    size_t count = 0;
    for (unsigned i = 0; i < 100; ++i)
    {
        assert(actual::scan(results, 16, count) && count == 4 && live_bytes == 0);
        started = false;
        assert(!actual::scan(results, 16, count) && live_bytes == 0);
        started = true;
        for (int* error : {&scan_error, &count_error, &records_error})
        {
            *error = -1;
            (void)actual::scan(results, 16, count);
            assert(live_bytes == 0);
            *error = 0;
        }
    }
    allocation_fails = true;
    assert(!actual::scan(results, 16, count) && live_bytes == 0);
    assert(std::strstr(message, "memory unavailable"));
}
