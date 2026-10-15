#include "bsp/trail_mate_t_display_p4_runtime.h"
#include "bsp/trail_mate_t_display_p4_keyboard.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "boards/t_display_p4/runtime_support.h"
#include "boards/t_display_p4/t_display_p4_board.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hi8561_driver.h"
#include "lvgl.h"
#include "platform/esp/idf_common/app_runtime_support.h"
#include "rm69a10_driver.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "ui/app_runtime.h"
#include "ui/ui_common.h"

#if !CONFIG_LVGL_PORT_ENABLE_PPA
#error "T-Display-P4 requires CONFIG_LVGL_PORT_ENABLE_PPA=y; CPU software rotation is unsupported"
#endif

extern "C" void trail_mate_idf_note_user_activity(void);

namespace
{

constexpr const char* kTag = "t-display-p4-ui";
constexpr int kDsiPhyLdoChannel = 3;
constexpr int kDsiPhyLdoMv = 2500;
constexpr uint32_t kLvglTimerPeriodMs = 1;
constexpr int kLvglTaskStackSize = 12288;
constexpr int kStartupBrightnessPercent = 10;
constexpr int kBacklightPwmHz = 2000;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
// Conservative I2C timeouts for touch — avoids random read failures
// when the bus is contended by expander / RTC / GPS/LoRa prepare.
// These will be tightened once the system I2C lock ordering is stable.
constexpr uint32_t kTouchI2cLockTimeoutMs = 50;
constexpr uint32_t kTouchI2cTransactionTimeoutMs = 30;
constexpr uint32_t kTouchI2cSpeedHz = 100000;
constexpr uint32_t kTouchI2cDiagnosticIntervalMs = 2000;

constexpr uint32_t kHi8561MemoryAddressEram = 0x20011000;
constexpr uint8_t kHi8561MaxDsramNum = 25;
constexpr uint32_t kHi8561DsramSectionInfoStartAddress = kHi8561MemoryAddressEram + 4;
constexpr uint32_t kHi8561EsramNumStartAddress =
    kHi8561DsramSectionInfoStartAddress + kHi8561MaxDsramNum * 8;
constexpr uint32_t kHi8561EsramSectionInfoStartAddress = kHi8561EsramNumStartAddress + 4;
constexpr uint16_t kHi8561MemoryEramSize = 4 * 1024;
constexpr uint8_t kHi8561TouchPointAddressOffset = 3;
constexpr uint8_t kHi8561SingleTouchPointDataSize = 5;

constexpr uint32_t kGt9895TouchInfoStartAddress = 0x00010308;
constexpr uint8_t kGt9895TouchPointAddressOffset = 8;
constexpr uint8_t kGt9895SingleTouchPointDataSize = 8;
constexpr uint8_t kGt9895MaxTouchFingerCount = 10;
constexpr uint32_t kAppLifecycleUiTimerIntervalMs = 10;
constexpr uint32_t kAppLifecycleUiEventsPerTick = 4;
constexpr uint32_t kDisplayLockDiagnosticIntervalMs = 2000;

esp_ldo_channel_handle_t s_dsi_phy_ldo = nullptr;
esp_lcd_dsi_bus_handle_t s_dsi_bus = nullptr;
esp_lcd_panel_io_handle_t s_panel_io = nullptr;
esp_lcd_panel_handle_t s_panel = nullptr;
lv_display_t* s_display = nullptr;
lv_indev_t* s_touch_indev = nullptr;
lv_timer_t* s_app_lifecycle_ui_timer = nullptr;
i2c_master_dev_handle_t s_touch_i2c_handle = nullptr;
bool s_lvgl_ready = false;
bool s_ready = false;
bool s_backlight_ready = false;
int s_brightness_percent = 0;
uint32_t s_hi8561_touch_info_start_address = 0;
uint32_t s_touch_last_i2c_error_log_ms = 0;
float s_touch_scale_x = 1.0f;
float s_touch_scale_y = 1.0f;

std::atomic<TaskHandle_t> s_display_lock_owner_task{nullptr};
std::atomic<TickType_t> s_display_lock_owner_since_ticks{0};
std::atomic<TaskHandle_t> s_display_lock_waiter_task{nullptr};
std::atomic<TickType_t> s_display_lock_waiter_since_ticks{0};
std::atomic<uint32_t> s_display_lock_waiter_timeout_ms{0};
std::atomic<TickType_t> s_display_lock_last_timeout_log_ticks{0};

namespace runtime_support = boards::t_display_p4::runtime_support;

const boards::t_display_p4::BoardProfile::PanelGeometry& active_panel()
{
    return runtime_support::active_panel();
}

bool use_hi8561_panel()
{
    return runtime_support::configured_panel_type() == boards::t_display_p4::DisplayPanelType::Hi8561;
}

const char* panel_variant_label()
{
    return use_hi8561_panel() ? "T-Display-P4 TFT" : "T-Display-P4 AMOLED";
}

lcd_color_rgb_pixel_format_t panel_pixel_format()
{
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PIXEL_FORMAT_RGB888)
    return LCD_COLOR_PIXEL_FORMAT_RGB888;
#else
    return LCD_COLOR_PIXEL_FORMAT_RGB565;
#endif
}

lv_color_format_t lvgl_color_format()
{
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PIXEL_FORMAT_RGB888)
    return LV_COLOR_FORMAT_RGB888;
#else
    return LV_COLOR_FORMAT_RGB565;
#endif
}

int bits_per_pixel()
{
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PIXEL_FORMAT_RGB888)
    return 24;
#else
    return 16;
#endif
}

bool ensure_dsi_phy_power()
{
    if (s_dsi_phy_ldo != nullptr)
    {
        return true;
    }

    esp_ldo_channel_config_t ldo_cfg{};
    ldo_cfg.chan_id = kDsiPhyLdoChannel;
    ldo_cfg.voltage_mv = kDsiPhyLdoMv;
    if (esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_phy_ldo) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to acquire DSI PHY LDO channel");
        return false;
    }
    return true;
}

bool init_backlight_backend()
{
    if (s_backlight_ready)
    {
        return true;
    }

    if (!use_hi8561_panel())
    {
        s_backlight_ready = true;
        return true;
    }

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = LEDC_TIMER_12_BIT;
    timer_cfg.timer_num = kBacklightTimer;
    timer_cfg.freq_hz = kBacklightPwmHz;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_cfg) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to configure backlight LEDC timer");
        return false;
    }

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = runtime_support::profile().lcd_backlight;
    channel_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_cfg.channel = kBacklightChannel;
    channel_cfg.intr_type = LEDC_INTR_DISABLE;
    channel_cfg.timer_sel = kBacklightTimer;
    channel_cfg.duty = 0;
    channel_cfg.hpoint = 0;
    if (ledc_channel_config(&channel_cfg) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to configure backlight LEDC channel");
        return false;
    }

    s_backlight_ready = true;
    return true;
}

bool ensure_touch_device()
{
    if (s_touch_i2c_handle != nullptr)
    {
        return true;
    }

    const runtime_support::SystemI2cDeviceConfig config{
        "touch",
        static_cast<uint16_t>(runtime_support::touch_i2c_address()),
        kTouchI2cSpeedHz,
    };
    s_touch_i2c_handle = runtime_support::get_managed_system_i2c_device(config, 1000);
    if (s_touch_i2c_handle == nullptr)
    {
        ESP_LOGE(kTag,
                 "Failed to acquire touch device addr=0x%02X",
                 runtime_support::touch_i2c_address());
        return false;
    }

    const auto& panel = active_panel();
    s_touch_scale_x = (panel.touch_max_x > 0)
                          ? static_cast<float>(panel.width) / static_cast<float>(panel.touch_max_x)
                          : 1.0f;
    s_touch_scale_y = (panel.touch_max_y > 0)
                          ? static_cast<float>(panel.height) / static_cast<float>(panel.touch_max_y)
                          : 1.0f;
    ESP_LOGI(kTag,
             "Touch bus ready panel=%s addr=0x%02X scale=(%.3f,%.3f)",
             use_hi8561_panel() ? "hi8561" : "rm69a10",
             static_cast<unsigned>(runtime_support::touch_i2c_address()),
             static_cast<double>(s_touch_scale_x),
             static_cast<double>(s_touch_scale_y));
    return true;
}

bool init_hi8561_touch_address_info()
{
    if (s_hi8561_touch_info_start_address != 0)
    {
        return true;
    }
    if (!ensure_touch_device() || !runtime_support::lock_system_i2c(1000))
    {
        return false;
    }

    const uint8_t request[] = {
        0xF3,
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress >> 24),
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress >> 16),
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress >> 8),
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress),
        0x03,
    };
    uint8_t response[48] = {};
    const esp_err_t err = i2c_master_transmit_receive(
        s_touch_i2c_handle, request, sizeof(request), response, sizeof(response), 1000);
    runtime_support::unlock_system_i2c();
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag, "HI8561 ESRAM probe failed: %s", esp_err_to_name(err));
        return false;
    }

    const uint32_t address = static_cast<uint32_t>(response[8]) |
                             (static_cast<uint32_t>(response[9]) << 8) |
                             (static_cast<uint32_t>(response[10]) << 16) |
                             (static_cast<uint32_t>(response[11]) << 24);
    if (address < kHi8561MemoryAddressEram ||
        address >= (kHi8561MemoryAddressEram + kHi8561MemoryEramSize))
    {
        ESP_LOGW(kTag, "HI8561 touch info address invalid: 0x%08lX", static_cast<unsigned long>(address));
        return false;
    }

    s_hi8561_touch_info_start_address = address;
    return true;
}

bool read_gt9895_touch(int32_t* out_x, int32_t* out_y, bool* out_pressed);

bool reset_touch_controller()
{
    s_hi8561_touch_info_start_address = 0;
    if (!runtime_support::reset_touch_controller(10, 10, 10))
    {
        ESP_LOGE(kTag, "Touch reset failed");
        return false;
    }
    return true;
}

bool probe_touch_controller()
{
    if (!ensure_touch_device())
    {
        return false;
    }

    if (use_hi8561_panel())
    {
        if (!init_hi8561_touch_address_info())
        {
            ESP_LOGE(kTag, "HI8561 touch probe failed");
            return false;
        }
        ESP_LOGI(kTag,
                 "HI8561 touch probe ok info=0x%08lX",
                 static_cast<unsigned long>(s_hi8561_touch_info_start_address));
        return true;
    }

    int32_t x = 0;
    int32_t y = 0;
    bool pressed = false;
    if (!read_gt9895_touch(&x, &y, &pressed))
    {
        ESP_LOGE(kTag, "GT9895 touch probe failed");
        return false;
    }
    ESP_LOGI(kTag, "GT9895 touch probe ok");
    return true;
}

bool read_hi8561_touch(int32_t* out_x, int32_t* out_y, bool* out_pressed)
{
    if (out_x == nullptr || out_y == nullptr || out_pressed == nullptr)
    {
        return false;
    }
    *out_pressed = false;

    if (!init_hi8561_touch_address_info())
    {
        return false;
    }
    if (!runtime_support::lock_system_i2c(kTouchI2cLockTimeoutMs))
    {
        const uint32_t now = esp_log_timestamp();
        if (now - s_touch_last_i2c_error_log_ms >= kTouchI2cDiagnosticIntervalMs)
        {
            s_touch_last_i2c_error_log_ms = now;
            ESP_LOGW(kTag, "HI8561 touch read skipped: SYS I2C lock timeout");
        }
        return false;
    }

    const uint32_t touch_point_address =
        s_hi8561_touch_info_start_address + kHi8561TouchPointAddressOffset;
    const uint8_t request[] = {
        0xF3,
        static_cast<uint8_t>(touch_point_address >> 24),
        static_cast<uint8_t>(touch_point_address >> 16),
        static_cast<uint8_t>(touch_point_address >> 8),
        static_cast<uint8_t>(touch_point_address),
        0x03,
    };
    uint8_t response[kHi8561SingleTouchPointDataSize] = {};
    const esp_err_t err = i2c_master_transmit_receive(
        s_touch_i2c_handle,
        request,
        sizeof(request),
        response,
        sizeof(response),
        kTouchI2cTransactionTimeoutMs);
    runtime_support::unlock_system_i2c();
    if (err != ESP_OK)
    {
        const uint32_t now = esp_log_timestamp();
        if (now - s_touch_last_i2c_error_log_ms >= kTouchI2cDiagnosticIntervalMs)
        {
            s_touch_last_i2c_error_log_ms = now;
            ESP_LOGW(kTag, "HI8561 touch read failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    const uint16_t raw_x =
        static_cast<uint16_t>((static_cast<uint16_t>(response[0]) << 8) | response[1]);
    const uint16_t raw_y =
        static_cast<uint16_t>((static_cast<uint16_t>(response[2]) << 8) | response[3]);
    if (raw_x == 0xFFFF && raw_y == 0xFFFF)
    {
        return true;
    }

    *out_x = std::clamp<int32_t>(raw_x, 0, active_panel().width - 1);
    *out_y = std::clamp<int32_t>(raw_y, 0, active_panel().height - 1);
    *out_pressed = true;
    return true;
}

bool read_gt9895_touch(int32_t* out_x, int32_t* out_y, bool* out_pressed)
{
    if (out_x == nullptr || out_y == nullptr || out_pressed == nullptr)
    {
        return false;
    }
    *out_pressed = false;

    if (!ensure_touch_device() || !runtime_support::lock_system_i2c(kTouchI2cLockTimeoutMs))
    {
        return false;
    }

    const uint8_t request[] = {
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress >> 24),
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress >> 16),
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress >> 8),
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress),
    };
    constexpr size_t kReadSize =
        kGt9895TouchPointAddressOffset + kGt9895MaxTouchFingerCount * kGt9895SingleTouchPointDataSize;
    uint8_t response[kReadSize] = {};
    const esp_err_t err = i2c_master_transmit_receive(
        s_touch_i2c_handle,
        request,
        sizeof(request),
        response,
        sizeof(response),
        kTouchI2cTransactionTimeoutMs);
    runtime_support::unlock_system_i2c();
    if (err != ESP_OK)
    {
        return false;
    }

    const bool edge_touch = response[0] == 0x84;
    const uint8_t finger_count = response[2];
    const uint8_t effective_touch_count =
        (finger_count == 0 && edge_touch) ? 1 : finger_count;
    if (effective_touch_count == 0 || effective_touch_count > kGt9895MaxTouchFingerCount)
    {
        return true;
    }

    const uint8_t offset = kGt9895TouchPointAddressOffset;
    const uint16_t raw_x = static_cast<uint16_t>(response[offset + 2] |
                                                 (static_cast<uint16_t>(response[offset + 3]) << 8));
    const uint16_t raw_y = static_cast<uint16_t>(response[offset + 4] |
                                                 (static_cast<uint16_t>(response[offset + 5]) << 8));
    const int32_t scaled_x = static_cast<int32_t>(raw_x * s_touch_scale_x);
    const int32_t scaled_y = static_cast<int32_t>(raw_y * s_touch_scale_y);
    *out_x = std::clamp<int32_t>(scaled_x, 0, active_panel().width - 1);
    *out_y = std::clamp<int32_t>(scaled_y, 0, active_panel().height - 1);
    *out_pressed = true;
    return true;
}

bool s_touch_was_pressed = false;
std::uint32_t s_last_activity_note_ms = 0;

void note_touch_activity(bool pressed)
{
    const std::uint32_t now = static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
    const bool first_touch = pressed && !s_touch_was_pressed;
    const bool throttle_ok = pressed && (now - s_last_activity_note_ms) >= 500U;

    s_touch_was_pressed = pressed;

    if (pressed && (first_touch || throttle_ok))
    {
        s_last_activity_note_ms = now;
        trail_mate_idf_note_user_activity();
    }
}

void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    (void)indev;
    int32_t x = 0;
    int32_t y = 0;
    bool pressed = false;
    const bool ok = use_hi8561_panel() ? read_hi8561_touch(&x, &y, &pressed)
                                       : read_gt9895_touch(&x, &y, &pressed);

    if (!ok || !pressed)
    {
        if (s_touch_was_pressed)
        {
            ESP_LOGI(kTag, "touch release");
        }
        note_touch_activity(false);
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (!s_touch_was_pressed)
    {
        ESP_LOGI(kTag, "touch press x=%ld y=%ld", static_cast<long>(x), static_cast<long>(y));
    }
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;

    // Debounce user-activity notification: only fire on the first touch
    // press edge, then at most once every 500 ms while the finger stays
    // down.  Without this throttle, every LVGL input poll (~50-100 Hz)
    // would hit the screen_sleep runtime and risk re-entrant UI updates.
    note_touch_activity(true);
}

bool create_touch_indev()
{
    if (!reset_touch_controller() || !probe_touch_controller())
    {
        return false;
    }

    if (!trail_mate_t_display_p4_display_lock(0)) return false;
    s_touch_indev = lv_indev_create();
    if (s_touch_indev == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create LVGL touch input device");
        trail_mate_t_display_p4_display_unlock();
        return false;
    }

    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, touch_read_cb);
    lv_indev_set_display(s_touch_indev, s_display);
    trail_mate_t_display_p4_display_unlock();
    ESP_LOGI(kTag, "LVGL touch input registered");
    return true;
}

void app_lifecycle_ui_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    platform::esp::idf_common::tickLvglTaskOwnedUiLifecycle(
        kAppLifecycleUiEventsPerTick);
}

bool start_app_lifecycle_ui_timer()
{
    if (s_app_lifecycle_ui_timer != nullptr)
    {
        return true;
    }
    if (!s_lvgl_ready || s_display == nullptr)
    {
        return false;
    }

    if (!trail_mate_t_display_p4_display_lock(1000))
    {
        ESP_LOGW(kTag, "Failed to start app lifecycle UI timer: LVGL lock timeout");
        return false;
    }

    s_app_lifecycle_ui_timer =
        lv_timer_create(app_lifecycle_ui_timer_cb,
                        kAppLifecycleUiTimerIntervalMs,
                        nullptr);
    trail_mate_t_display_p4_display_unlock();

    if (s_app_lifecycle_ui_timer == nullptr)
    {
        ESP_LOGW(kTag, "Failed to create app lifecycle UI timer");
        return false;
    }

    platform::esp::idf_common::setLvglTaskOwnedUiDispatch(true);
    ESP_LOGI(kTag,
             "LVGL task-owned app UI dispatch enabled interval=%lums events=%u",
             static_cast<unsigned long>(kAppLifecycleUiTimerIntervalMs),
             static_cast<unsigned>(kAppLifecycleUiEventsPerTick));
    return true;
}

bool create_panel()
{
    if (s_panel != nullptr)
    {
        return true;
    }
    if (!ensure_dsi_phy_power())
    {
        return false;
    }

    const auto& panel = active_panel();

    esp_lcd_dsi_bus_config_t bus_cfg{};
    bus_cfg.bus_id = 0;
    bus_cfg.num_data_lanes = static_cast<uint8_t>(panel.lane_num);
    bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = static_cast<uint32_t>(panel.lane_bit_rate_mbps);
    if (esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to create DSI bus");
        return false;
    }

    esp_lcd_dbi_io_config_t dbi_cfg{};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = 8;
    dbi_cfg.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_panel_io) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to create DSI DBI IO");
        return false;
    }

    esp_lcd_dpi_panel_config_t dpi_cfg{};
    dpi_cfg.virtual_channel = 0;
    dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = static_cast<uint32_t>(panel.dpi_clock_mhz);
    dpi_cfg.pixel_format = panel_pixel_format();
    // LVGL owns an independent partial-rendering buffer. The DPI driver is a
    // transfer endpoint, never an application-managed front/back swapchain.
    dpi_cfg.num_fbs = 0;
    dpi_cfg.video_timing.h_size = static_cast<uint32_t>(panel.width);
    dpi_cfg.video_timing.v_size = static_cast<uint32_t>(panel.height);
    dpi_cfg.video_timing.hsync_pulse_width = static_cast<uint32_t>(panel.hsync);
    dpi_cfg.video_timing.hsync_back_porch = static_cast<uint32_t>(panel.hbp);
    dpi_cfg.video_timing.hsync_front_porch = static_cast<uint32_t>(panel.hfp);
    dpi_cfg.video_timing.vsync_pulse_width = static_cast<uint32_t>(panel.vsync);
    dpi_cfg.video_timing.vsync_back_porch = static_cast<uint32_t>(panel.vbp);
    dpi_cfg.video_timing.vsync_front_porch = static_cast<uint32_t>(panel.vfp);
    dpi_cfg.flags.use_dma2d = true;

    esp_err_t err = ESP_FAIL;
    if (use_hi8561_panel())
    {
        hi8561_vendor_config_t vendor_cfg{};
        vendor_cfg.init_cmds = nullptr;
        vendor_cfg.init_cmds_size = 0;
        vendor_cfg.mipi_config.dsi_bus = s_dsi_bus;
        vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
        vendor_cfg.mipi_config.lane_num = static_cast<uint8_t>(panel.lane_num);

        esp_lcd_panel_dev_config_t panel_cfg{};
        panel_cfg.reset_gpio_num = -1;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = bits_per_pixel();
        panel_cfg.vendor_config = &vendor_cfg;
        err = esp_lcd_new_panel_hi8561(s_panel_io, &panel_cfg, &s_panel);
    }
    else
    {
        rm69a10_vendor_config_t vendor_cfg{};
        vendor_cfg.init_cmds = nullptr;
        vendor_cfg.init_cmds_size = 0;
        vendor_cfg.mipi_config.dsi_bus = s_dsi_bus;
        vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
        vendor_cfg.mipi_config.lane_num = static_cast<uint8_t>(panel.lane_num);

        esp_lcd_panel_dev_config_t panel_cfg{};
        panel_cfg.reset_gpio_num = -1;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = bits_per_pixel();
        panel_cfg.vendor_config = &vendor_cfg;
        err = esp_lcd_new_panel_rm69a10(s_panel_io, &panel_cfg, &s_panel);
    }
    if (err != ESP_OK || s_panel == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create LCD panel");
        return false;
    }

    if (esp_lcd_panel_reset(s_panel) != ESP_OK ||
        esp_lcd_panel_init(s_panel) != ESP_OK ||
        esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to initialize LCD panel");
        return false;
    }

    return true;
}

bool init_lvgl()
{
    if (s_lvgl_ready)
    {
        return true;
    }

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.timer_period_ms = kLvglTimerPeriodMs;
    lvgl_cfg.task_stack = kLvglTaskStackSize;
    if (lvgl_port_init(&lvgl_cfg) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to initialize LVGL port");
        return false;
    }

    s_lvgl_ready = true;
    return true;
}

bool create_display()
{
    if (s_display != nullptr)
    {
        return true;
    }
    if (!create_panel() || !init_lvgl())
    {
        return false;
    }

    const auto& panel = active_panel();
    const uint32_t draw_buffer_pixels = static_cast<uint32_t>(panel.width) *
                                        static_cast<uint32_t>(panel.height);

    // esp_lvgl_port owns the LVGL draw buffer and PPA output buffer. With
    // CONFIG_LVGL_PORT_ENABLE_PPA=y, sw_rotate selects PPA acceleration for
    // logical 90-degree rotation; it does not use LVGL's CPU software rotate.
    lvgl_port_display_cfg_t display_cfg{};
    display_cfg.io_handle = s_panel_io;
    display_cfg.panel_handle = s_panel;
    display_cfg.buffer_size = draw_buffer_pixels;
    display_cfg.double_buffer = false;
    display_cfg.hres = static_cast<uint32_t>(panel.width);
    display_cfg.vres = static_cast<uint32_t>(panel.height);
    display_cfg.monochrome = false;
    display_cfg.color_format = lvgl_color_format();
    display_cfg.flags.buff_dma = true;
    display_cfg.flags.buff_spiram = true;
    display_cfg.flags.sw_rotate = true;
    display_cfg.flags.swap_bytes = false;
    display_cfg.flags.full_refresh = false;
    display_cfg.flags.direct_mode = false;

    // Avoid-tearing would reintroduce panel-owned LVGL buffers and refresh
    // boundary synchronization. Color-transfer completion is the correct
    // ownership boundary for this independent-buffer path.
    lvgl_port_display_dsi_cfg_t dsi_cfg{};
    dsi_cfg.flags.avoid_tearing = false;

    s_display = lvgl_port_add_disp_dsi(&display_cfg, &dsi_cfg);
    if (s_display == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create P4 DSI LVGL display");
        return false;
    }
    lv_display_set_rotation(s_display, LV_DISPLAY_ROTATION_90);
    return true;
}

void create_boot_screen()
{
    if (s_display == nullptr || !trail_mate_t_display_p4_display_lock(1000))
    {
        return;
    }

    lv_obj_t* screen = lv_screen_active();
    if (screen != nullptr)
    {
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x07131F), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* title = lv_label_create(screen);
        lv_label_set_text(title, "TrailMate");
        lv_obj_set_style_text_color(title, lv_color_hex(0xE7F4FF), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -16);

        lv_obj_t* subtitle = lv_label_create(screen);
        lv_label_set_text(subtitle, panel_variant_label());
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0x6DB8E8), 0);
        lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 14);
    }

    trail_mate_t_display_p4_display_unlock();
}

const char* task_name_or(TaskHandle_t task, const char* fallback)
{
    return task != nullptr ? pcTaskGetName(task) : fallback;
}

void clear_display_lock_waiter(TaskHandle_t requester)
{
    TaskHandle_t expected = requester;
    if (s_display_lock_waiter_task.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel))
    {
        s_display_lock_waiter_since_ticks.store(0, std::memory_order_relaxed);
        s_display_lock_waiter_timeout_ms.store(0, std::memory_order_relaxed);
    }
}

void log_display_lock_timeout(TaskHandle_t requester, uint32_t timeout_ms)
{
    const TickType_t now = xTaskGetTickCount();
    TickType_t last_log =
        s_display_lock_last_timeout_log_ticks.load(std::memory_order_relaxed);
    if ((now - last_log) < pdMS_TO_TICKS(kDisplayLockDiagnosticIntervalMs) ||
        !s_display_lock_last_timeout_log_ticks.compare_exchange_strong(
            last_log, now, std::memory_order_relaxed))
    {
        return;
    }

    const TaskHandle_t owner =
        s_display_lock_owner_task.load(std::memory_order_acquire);
    const TickType_t owner_since =
        s_display_lock_owner_since_ticks.load(std::memory_order_relaxed);
    const TaskHandle_t waiter =
        s_display_lock_waiter_task.load(std::memory_order_acquire);
    const TickType_t waiter_since =
        s_display_lock_waiter_since_ticks.load(std::memory_order_relaxed);
    const uint32_t waiter_timeout_ms =
        s_display_lock_waiter_timeout_ms.load(std::memory_order_relaxed);
    const uint32_t held_ms =
        (owner != nullptr && owner_since != 0)
            ? static_cast<uint32_t>((now - owner_since) * portTICK_PERIOD_MS)
            : 0;
    const uint32_t waiting_ms =
        (waiter != nullptr && waiter_since != 0)
            ? static_cast<uint32_t>((now - waiter_since) * portTICK_PERIOD_MS)
            : 0;

    ESP_LOGW(kTag,
             "LVGL display lock timeout requester=%s owner=%s held_ms=%lu waiter=%s waiting_ms=%lu waiter_timeout_ms=%lu request_timeout_ms=%lu",
             task_name_or(requester, "unknown"),
             task_name_or(owner, "lvgl-port/untracked"),
             static_cast<unsigned long>(held_ms),
             task_name_or(waiter, "none"),
             static_cast<unsigned long>(waiting_ms),
             static_cast<unsigned long>(waiter_timeout_ms),
             static_cast<unsigned long>(timeout_ms));
}

esp_err_t set_brightness_percent(int brightness_percent)
{
    const int clamped = std::clamp(brightness_percent, 0, 100);
    s_brightness_percent = clamped;

    if (use_hi8561_panel())
    {
        if (!init_backlight_backend())
        {
            return ESP_FAIL;
        }

        const uint32_t duty = static_cast<uint32_t>((4095U * static_cast<uint32_t>(clamped)) / 100U);
        if (ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty) != ESP_OK ||
            ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel) != ESP_OK)
        {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    if (s_panel == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t raw = static_cast<uint8_t>((255U * static_cast<uint32_t>(clamped)) / 100U);
    return set_rm69a10_brightness(s_panel, raw);
}

} // namespace

extern "C" bool trail_mate_t_display_p4_display_runtime_init(void)
{
    if (s_ready)
    {
        return true;
    }

    ESP_LOGI(kTag,
             "Initializing %s display runtime panel=%s size=%dx%d",
             panel_variant_label(),
             use_hi8561_panel() ? "hi8561" : "rm69a10",
             active_panel().width,
             active_panel().height);

    // Keep the first P2 software-I2C probe outside the LVGL/PPA render task.
    // LilyGO's working P4 keyboard example brings up XL9555/TCA8418 before it
    // creates LVGL.  Trail's create_display() starts that task as part of
    // display creation, so perform the vendor-controller phase first while the
    // P4 board power sequence is already complete and UI rendering is absent.
    const bool keyboard_controller_ready = trail_mate_t_display_p4_keyboard_initialize();
    ESP_LOGI(kTag,
             "P4 keyboard controller pre-LVGL initialization ready=%u",
             keyboard_controller_ready ? 1U : 0U);

    if (!create_display())
    {
        return false;
    }
    if (!init_backlight_backend())
    {
        return false;
    }
    if (!create_touch_indev())
    {
        return false;
    }
    if (set_brightness_percent(kStartupBrightnessPercent) != ESP_OK)
    {
        ESP_LOGW(kTag, "Initial brightness update failed");
    }

    create_boot_screen();
    (void)start_app_lifecycle_ui_timer();
    // Start the keypad and recovery timer even if the early controller probe
    // failed. A missing keyboard can attach later without restarting the UI.
    bool keyboard_ready = false;
    if (trail_mate_t_display_p4_display_lock(0))
    {
        keyboard_ready = trail_mate_t_display_p4_keyboard_start();
        trail_mate_t_display_p4_display_unlock();
    }
    if (!keyboard_ready)
    {
        ESP_LOGI(kTag, "T-Display-P4 keyboard module is not available");
    }

    s_ready = true;
    ESP_LOGI(kTag,
             "Display runtime ready buffer_pixels=%lu buffer_mode=three-surface-vsync brightness=%d",
             static_cast<unsigned long>(static_cast<uint32_t>(active_panel().width) *
                                        static_cast<uint32_t>(active_panel().height)),
             s_brightness_percent);
    return true;
}

extern "C" bool trail_mate_t_display_p4_display_runtime_is_ready(void)
{
    return s_ready;
}

extern "C" bool trail_mate_t_display_p4_display_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_ready)
    {
        return false;
    }

    const TaskHandle_t requester = xTaskGetCurrentTaskHandle();
    s_display_lock_waiter_since_ticks.store(xTaskGetTickCount(), std::memory_order_relaxed);
    s_display_lock_waiter_timeout_ms.store(timeout_ms, std::memory_order_relaxed);
    s_display_lock_waiter_task.store(requester, std::memory_order_release);

    if (lvgl_port_lock(timeout_ms))
    {
        clear_display_lock_waiter(requester);
        s_display_lock_owner_since_ticks.store(xTaskGetTickCount(), std::memory_order_relaxed);
        s_display_lock_owner_task.store(requester, std::memory_order_release);
        return true;
    }

    log_display_lock_timeout(requester, timeout_ms);
    clear_display_lock_waiter(requester);
    return false;
}

extern "C" void trail_mate_t_display_p4_display_unlock(void)
{
    if (s_lvgl_ready)
    {
        s_display_lock_owner_task.store(nullptr, std::memory_order_release);
        s_display_lock_owner_since_ticks.store(0, std::memory_order_relaxed);
        lvgl_port_unlock();
    }
}

extern "C" esp_err_t trail_mate_t_display_p4_display_set_brightness_percent(int brightness_percent)
{
    return set_brightness_percent(brightness_percent);
}
