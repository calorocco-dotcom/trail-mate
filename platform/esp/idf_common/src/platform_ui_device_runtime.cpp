#include "platform/ui/device_runtime.h"

#include "platform/audio/media_volume.h"
#include <cstring>
#include <ctime>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "boards/tab5/tab5_board.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/common/build_info.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/gps_runtime.h"
#include "platform/esp/idf_common/reticulum_call_runtime_support.h"
#include "platform/ui/settings_store.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "bsp/m5stack_tab5.h"
#endif

namespace platform::ui::device
{

namespace
{

constexpr uint8_t kIdfMaxBrightnessLevel = 16U;
uint8_t s_brightness_level = kIdfMaxBrightnessLevel;
constexpr ::time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC

bool is_valid_epoch(::time_t value)
{
    return value >= kMinValidEpochSeconds;
}

} // namespace

void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void restart()
{
    esp_restart();
}

bool rtc_ready()
{
    return is_valid_epoch(std::time(nullptr));
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    info.charging = bsp_usb_c_detect();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
    info.available = true;
    info.charging = board.isCharging();
    info.level = board.getBatteryLevel();
#endif
    return info;
}

MemoryStats memory_stats()
{
    MemoryStats stats{};
    stats.ram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    stats.ram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    stats.psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    stats.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    stats.psram_available = stats.psram_total_bytes > 0;
    return stats;
}

const char* firmware_version()
{
    const char* configured = ::platform::esp::common::build_info::firmwareVersion();
    if (configured && configured[0] != '\0' && std::strcmp(configured, "unknown") != 0)
    {
        return configured;
    }

    const esp_app_desc_t* desc = esp_app_get_description();
    return (desc && desc->version[0] != '\0') ? desc->version : "unknown";
}

void handle_low_battery(const BatteryInfo& info)
{
    (void)info;
}

bool supports_screen_brightness()
{
    return true;
}

bool supports_keyboard_backlight()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::supportsKeyboardBacklight();
#else
    return false;
#endif
}

bool supports_configurable_battery_gauge()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return true;
#else
    return false;
#endif
}

void reload_configurable_battery_gauge()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    constexpr uint16_t kDefaultCapacityMah = 1500;
    const uint32_t configured_design = settings_store::get_uint(
        "power", "gauge_design_mah", kDefaultCapacityMah);
    const uint32_t configured_full = settings_store::get_uint(
        "power", "gauge_full_mah", kDefaultCapacityMah);
    const uint16_t design = configured_design > 0 && configured_design <= 10000
                                ? static_cast<uint16_t>(configured_design)
                                : kDefaultCapacityMah;
    const uint16_t full = configured_full > 0 && configured_full <= 10000
                              ? static_cast<uint16_t>(configured_full)
                              : kDefaultCapacityMah;
    (void)::boards::t_display_p4::TDisplayP4Board::instance()
        .configureBatteryGaugeCapacity(design, full);
#endif
}

uint8_t screen_brightness()
{
    return s_brightness_level;
}

uint8_t screen_brightness_max()
{
    return kIdfMaxBrightnessLevel;
}

void set_screen_brightness(uint8_t level)
{
    const uint8_t max_level = screen_brightness_max();
    const uint8_t clamped = level > max_level ? max_level : level;
    s_brightness_level = clamped;
    const int percent = (kIdfMaxBrightnessLevel == 0U)
                            ? 100
                            : static_cast<int>((static_cast<uint32_t>(clamped) * 100U) /
                                               static_cast<uint32_t>(
                                                   kIdfMaxBrightnessLevel));
    (void)platform::esp::idf_common::bsp_runtime::set_display_brightness(percent);
}

uint8_t keyboard_backlight()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
    return board.supportsKeyboardBacklight() ? board.keyboardGetBrightness() : 0;
#else
    return 0;
#endif
}

uint8_t keyboard_backlight_max()
{
    return kIdfMaxBrightnessLevel;
}

void set_keyboard_backlight(uint8_t level)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
    if (!board.supportsKeyboardBacklight())
    {
        return;
    }
    const uint8_t clamped =
        level > kIdfMaxBrightnessLevel ? kIdfMaxBrightnessLevel : level;
    board.keyboardSetBrightness(clamped);
#else
    (void)level;
#endif
}

void trigger_haptic()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    ::boards::t_display_p4::TDisplayP4Board::instance().vibrator();
#endif
}

uint8_t default_message_tone_volume()
{
    return 45;
}

void set_message_tone_volume(uint8_t volume_percent)
{
    const uint8_t volume = volume_percent > 100U ? 100U : volume_percent;
    ::platform::esp::idf_common::reticulum_call_support::set_speaker_volume(volume);
    ::platform::audio::publish_media_volume(volume);
}

void play_message_tone()
{
    (void)::platform::esp::idf_common::reticulum_call_support::play_message_notification();
}

bool sd_ready()
{
    return platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
}

bool card_ready()
{
    return sd_ready();
}

bool gps_ready()
{
    return platform::esp::idf_common::gps_runtime::is_enabled() ||
           platform::esp::idf_common::gps_runtime::is_powered();
}

bool gps_supported()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasGpsUart();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::hasGpsUart();
#else
    return false;
#endif
}

int power_tier()
{
    const BatteryInfo info = battery_info();
    if (!info.available || info.level < 0)
    {
        return 0;
    }
    return info.level <= 15 ? 1 : 0;
}

} // namespace platform::ui::device
