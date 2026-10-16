#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>

#define ESP_LOGE(...) ((void)0)
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = -2, ESP_ERR_TIMEOUT = -3;
constexpr int LV_DISPLAY_ROTATION_90 = 1, LEDC_LOW_SPEED_MODE = 0, kBacklightChannel = 0;
int lock_depth = 0;
bool lock_ok = true, allocation_ok = true, transfer_ok = true, hi8561 = true;
std::atomic<bool> s_ready{false};
struct Display
{
    int rotation = 0;
} display;
Display* s_display = nullptr;
void* s_panel = &display;
void* s_panel_io = &display;
int s_brightness_percent = 0;
struct Panel
{
    int width = 540, height = 1168;
} panel;
const auto& active_panel() { return panel; }
bool create_panel() { return true; }
bool init_lvgl() { return true; }
bool init_backlight_backend() { return true; }
int lvgl_color_format() { return 16; }
bool use_hi8561_panel() { return hi8561; }
struct lvgl_port_display_cfg_t
{
    void* io_handle;
    void* panel_handle;
    uint32_t buffer_size, hres, vres;
    bool double_buffer, monochrome;
    int color_format;
    struct
    {
        bool buff_dma, buff_spiram, sw_rotate, swap_bytes, full_refresh, direct_mode;
    } flags;
};
struct lvgl_port_display_dsi_cfg_t
{
    struct
    {
        bool avoid_tearing;
    } flags;
};
bool trail_mate_t_display_p4_display_lock(uint32_t)
{
    if (!lock_ok) return false;
    ++lock_depth;
    return true;
}
void trail_mate_t_display_p4_display_unlock()
{
    assert(lock_depth > 0);
    --lock_depth;
}
Display* lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t* config, const lvgl_port_display_dsi_cfg_t*)
{
    assert(lock_depth > 0);
    assert(config->flags.sw_rotate && !config->flags.direct_mode && !config->flags.full_refresh);
    return allocation_ok ? &display : nullptr;
}
void lv_display_set_rotation(Display* target, int rotation)
{
    assert(lock_depth > 0);
    target->rotation = rotation;
}
int ledc_set_duty(int, int, uint32_t)
{
    assert(lock_depth > 0);
    return transfer_ok ? ESP_OK : ESP_FAIL;
}
int ledc_update_duty(int, int)
{
    assert(lock_depth > 0);
    return transfer_ok ? ESP_OK : ESP_FAIL;
}
int set_rm69a10_brightness(void*, uint8_t)
{
    assert(lock_depth > 0);
    return transfer_ok ? ESP_OK : ESP_FAIL;
}
#include "display_actual.inc"

int main()
{
    assert(create_display());
    assert(display.rotation == LV_DISPLAY_ROTATION_90 && lock_depth == 0);
    s_display = nullptr;
    allocation_ok = false;
    assert(!create_display() && lock_depth == 0);
    allocation_ok = true;
    assert(trail_mate_t_display_p4_display_set_brightness_percent(40) == ESP_ERR_INVALID_STATE);
    s_ready = true;
    lock_ok = false;
    assert(trail_mate_t_display_p4_display_set_brightness_percent(40) == ESP_ERR_TIMEOUT);
    lock_ok = true;
    for (const bool is_tft : {true, false})
    {
        hi8561 = is_tft;
        transfer_ok = true;
        assert(trail_mate_t_display_p4_display_set_brightness_percent(40) == ESP_OK);
        assert(s_brightness_percent == 40 && lock_depth == 0);
        transfer_ok = false;
        assert(trail_mate_t_display_p4_display_set_brightness_percent(80) == ESP_FAIL);
        assert(s_brightness_percent == 40 && lock_depth == 0);
    }
    std::cout << "P4 rotation/brightness serialization and failed-transfer cache passed\n";
}
