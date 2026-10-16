#include "hostlink/c6/c6_protocol.h"
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
    int8_t rssi = -127;
    uint8_t primary = 1, authmode = 0;
};
struct wifi_scan_config_t
{
};
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_NO_MEM = -1;
bool enabled = true, started = true;
bool driver_list_live = false;
bool scan_needs_scratch = true;
unsigned driver_list_clears = 0;
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
    assert(!scan_needs_scratch || live_bytes == sizeof(wifi_ap_record_t) * 16);
    if (scan_error == ESP_OK) driver_list_live = true;
    return scan_error;
}
int esp_wifi_scan_get_ap_num(uint16_t* count)
{
    *count = ap_count;
    return count_error;
}
int esp_wifi_scan_get_ap_records(uint16_t* count, wifi_ap_record_t* out)
{
    *count = std::min(*count, ap_count);
    for (uint16_t i = 0; i < *count; ++i) std::strcpy(out[i].ssid, "test");
    if (records_error == ESP_OK) driver_list_live = false;
    return records_error;
}
int esp_wifi_clear_ap_list()
{
    driver_list_live = false;
    ++driver_list_clears;
    return ESP_OK;
}
void insert_scan_result_sorted(ScanResult*, size_t, size_t& count, const wifi_ap_record_t&) { ++count; }
namespace actual
{
#include "wifi_scan.inc"
}
namespace companion
{
constexpr int WIFI_MODE_STA = 1;
int ensure_wifi_started_with_mode(int) { return started ? ESP_OK : -1; }
void tm_services_record_error(int, const char*) {}
unsigned scan_errors = 0;
void emit_simple_event(int kind, int, const char*, unsigned)
{
    assert(kind == TM_C6_WIFI_EVENT_ERROR);
    ++scan_errors;
}
void copy_text(char* out, size_t length, const char* in) { std::snprintf(out, length, "%s", in); }
unsigned scan_events = 0;
bool tm_services_send_wifi_event(const tm_c6_wifi_event_t* event)
{
    assert(event->event_kind == TM_C6_WIFI_EVENT_SCAN_DONE && event->result_count == ap_count);
    ++scan_events;
    return true;
}
#define ESP_RETURN_ON_ERROR(call, ...)       \
    do                                       \
    {                                        \
        const auto result = (call);          \
        if (result != ESP_OK) return result; \
    } while (false)
#include "c6_wifi_scan.inc"
#undef ESP_RETURN_ON_ERROR
} // namespace companion
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
            assert(!actual::scan(results, 16, count));
            assert(live_bytes == 0 && !driver_list_live);
            *error = 0;
        }
    }
    allocation_fails = true;
    assert(!actual::scan(results, 16, count) && live_bytes == 0);
    assert(std::strstr(message, "memory unavailable"));
    allocation_fails = false;
    ap_count = 0;
    const auto clears = driver_list_clears;
    assert(actual::scan(results, 16, count) && count == 0 && !driver_list_live);
    assert(driver_list_clears == clears + 1);
    scan_needs_scratch = false;
    ap_count = 4;
    for (unsigned cycle = 0; cycle < 100; ++cycle)
    {
        assert(companion::emit_scan_results() == ESP_OK && !driver_list_live);
        for (int* error : {&count_error, &records_error})
        {
            *error = -1;
            assert(companion::emit_scan_results() == -1 && !driver_list_live);
            *error = 0;
        }
    }
    assert(companion::scan_events == 100);
    assert(companion::scan_errors == 200);
    ap_count = 0;
    assert(companion::emit_scan_results() == ESP_OK && !driver_list_live);
}
