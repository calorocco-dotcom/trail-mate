#pragma once

#include <cstdint>

namespace boards::t_display_p4
{

enum class DisplayPanelType : uint8_t
{
    Hi8561 = 0,
    Rm69a10,
};

struct BoardProfile
{
    struct ProductIdentity
    {
        const char* long_name = "TrailMate P4";
        const char* short_name = "TMP4";
        const char* ble_name = "trailmate-p4";
    };

    struct I2cBus
    {
        int port = -1;
        int sda = -1;
        int scl = -1;
    };

    struct UartPort
    {
        int port = -1;
        int tx = -1;
        int rx = -1;
        int aux = -1;
        uint32_t baud_rate = 38400;
    };

    struct SdmmcPins
    {
        int d0 = -1;
        int d1 = -1;
        int d2 = -1;
        int d3 = -1;
        int cmd = -1;
        int clk = -1;
    };

    struct CompanionSdioPins
    {
        int clk = -1;
        int cmd = -1;
        int d0 = -1;
        int d1 = -1;
        int d2 = -1;
        int d3 = -1;
    };

    struct AudioI2sPins
    {
        int bclk = -1;
        int mclk = -1;
        int ws = -1;
        int dout = -1;
        int din = -1;
    };

    struct LoRaModulePins
    {
        struct SpiPins
        {
            int host = -1;
            int sck = -1;
            int miso = -1;
            int mosi = -1;
        };

        SpiPins spi{};
        int nss = -1;
        int rst = -1;
        int irq = -1;
        int busy = -1;
        int pwr_en = -1;
        int rf_switch = -1;
    };

    struct IoExpanderPins
    {
        int power_3v3 = -1;
        int power_5v = -1; // Legacy name: XL9535 IO6 is AUDIO_POWER_EN on P4 V1.
        int p4_vcca = -1;  // Legacy name: XL9535 IO10 is USB_PHY_POWER_EN on P4 V1.
        int screen_rst = -1;
        int touch_rst = -1;
        int touch_int = -1;
        int ethernet_rst = -1;
        int gps_wake = -1;
        int rtc_int = -1;
        int c6_wake = -1;
        int c6_boot = -1;
        int c6_enable = -1;
        int sd_enable = -1;
        int lora_rst = -1;
        int lora_dio1 = -1;
        int lora_rf_switch = -1;
    };

    struct I2cAddresses
    {
        uint16_t io_expander = 0;
        uint16_t rtc = 0;
        uint16_t battery_gauge = 0;
        uint16_t haptic = 0;
        uint16_t hi8561_touch = 0;
        uint16_t gt9895_touch = 0;
        uint16_t imu = 0;
    };

    struct KeyboardModule
    {
        int sda = -1;
        int scl = -1;
        int interrupt = -1;
        int backlight = -1;
        uint16_t tca8418 = 0;
        uint16_t xl9555 = 0;
        int rows = 0;
        int columns = 0;
    };

    struct PanelGeometry
    {
        int width = 0;
        int height = 0;
        int dpi_clock_mhz = 0;
        int hsync = 0;
        int hbp = 0;
        int hfp = 0;
        int vsync = 0;
        int vbp = 0;
        int vfp = 0;
        int lane_num = 2;
        int lane_bit_rate_mbps = 1000;
        uint16_t touch_address = 0;
        int touch_max_x = 0;
        int touch_max_y = 0;
    };

    ProductIdentity identity{};
    I2cBus sys_i2c{};
    I2cBus ext_i2c{};
    UartPort gps_uart{};
    SdmmcPins sdmmc{};
    CompanionSdioPins c6_sdio{};
    AudioI2sPins audio_i2s{};
    LoRaModulePins lora{};
    IoExpanderPins io_expander{};
    I2cAddresses i2c{};
    KeyboardModule keyboard{};
    PanelGeometry hi8561_panel{};
    PanelGeometry rm69a10_panel{};
    int boot = -1;
    int expander_int = -1;
    int lcd_backlight = -1;
    DisplayPanelType default_panel = DisplayPanelType::Hi8561;
    bool sd_enable_active_low = true;
    bool power_3v3_active_high = false;
    bool power_5v_active_high = true;
    bool p4_vcca_active_high = true;
    bool gps_wake_active_high = true;
    bool c6_enable_active_high = true;
    bool c6_wake_active_high = true;
    bool c6_boot_active_low = true;
    bool screen_reset_active_low = true;
    bool touch_reset_active_low = true;
    bool lora_reset_active_low = true;
    bool lora_dio1_via_expander = false;
    bool lora_reset_via_expander = false;
    bool lora_rf_switch_via_expander = false;
    bool has_display = false;
    bool has_touch = false;
    bool has_audio = false;
    bool has_sdcard = false;
    bool has_gps_uart = false;
    bool has_lora = false;
    bool has_c6_companion = false;
    bool has_motion_sensor = false;
    bool has_haptic = false;
    bool supports_keyboard_module = false;
    const char* motion_sensor = nullptr;
    bool uses_io_expander_for_lora = false;
};

inline constexpr BoardProfile makeBoardProfile()
{
    BoardProfile profile{};

    profile.identity.long_name = "TrailMate P4";
    profile.identity.short_name = "TMP4";
    profile.identity.ble_name = "trailmate-p4";

    // ESP32-P4 LP_I2C_NUM_0 (2): LilyGO's SYS bus uses LP-capable GPIO7/8.
    // Reserve HP I2C0 for the keyboard and HP I2C1 for the external devices.
    profile.sys_i2c = {2, 7, 8};
    profile.ext_i2c = {1, 20, 21};
    // The LilyGO signal names are from the GNSS module's perspective:
    // GPS_RX (GPIO23) is the P4 UART TX pin and GPS_TX (GPIO22) is the P4
    // UART RX pin. The official L76K example configures the module at 115200.
    profile.gps_uart = {1, 23, 22, -1, 115200};
    profile.sdmmc = {39, 40, 41, 42, 44, 43};
    profile.c6_sdio = {18, 19, 14, 15, 16, 17};
    profile.audio_i2s = {12, 13, 9, 10, 11};
    profile.lora.spi = {1, 2, 4, 3};
    profile.lora.nss = 24;
    profile.lora.rst = -1;
    profile.lora.irq = -1;
    profile.lora.busy = 6;
    profile.lora.pwr_en = -1;
    profile.lora.rf_switch = -1;

    profile.io_expander.power_3v3 = 0;
    profile.io_expander.power_5v = 6;
    profile.io_expander.p4_vcca = 10;
    profile.io_expander.screen_rst = 2;
    profile.io_expander.touch_rst = 3;
    profile.io_expander.touch_int = 4;
    profile.io_expander.ethernet_rst = 5;
    profile.io_expander.gps_wake = 11;
    profile.io_expander.rtc_int = 12;
    profile.io_expander.c6_wake = 13;
    profile.io_expander.c6_boot = 13;
    profile.io_expander.c6_enable = 14;
    profile.io_expander.sd_enable = 15;
    profile.io_expander.lora_rst = 16;
    profile.io_expander.lora_dio1 = 17;
    profile.io_expander.lora_rf_switch = 1;

    profile.i2c.io_expander = 0x20;
    profile.i2c.rtc = 0x51;
    profile.i2c.battery_gauge = 0x55;
    profile.i2c.haptic = 0x58;
    profile.i2c.hi8561_touch = 0x68;
    profile.i2c.gt9895_touch = 0x5D;
    profile.i2c.imu = 0x68;

    profile.keyboard.sda = 46;
    profile.keyboard.scl = 45;
    profile.keyboard.interrupt = 48;
    profile.keyboard.backlight = 47;
    profile.keyboard.tca8418 = 0x34;
    profile.keyboard.xl9555 = 0x20;
    profile.keyboard.rows = 7;
    profile.keyboard.columns = 10;

    profile.hi8561_panel.width = 540;
    profile.hi8561_panel.height = 1168;
    profile.hi8561_panel.dpi_clock_mhz = 60;
    profile.hi8561_panel.hsync = 28;
    profile.hi8561_panel.hbp = 26;
    profile.hi8561_panel.hfp = 20;
    profile.hi8561_panel.vsync = 2;
    profile.hi8561_panel.vbp = 22;
    profile.hi8561_panel.vfp = 200;
    profile.hi8561_panel.lane_num = 2;
    profile.hi8561_panel.lane_bit_rate_mbps = 1000;
    profile.hi8561_panel.touch_address = profile.i2c.hi8561_touch;
    profile.hi8561_panel.touch_max_x = profile.hi8561_panel.width;
    profile.hi8561_panel.touch_max_y = profile.hi8561_panel.height;

    profile.rm69a10_panel.width = 568;
    profile.rm69a10_panel.height = 1232;
    profile.rm69a10_panel.dpi_clock_mhz = 60;
    profile.rm69a10_panel.hsync = 50;
    profile.rm69a10_panel.hbp = 150;
    profile.rm69a10_panel.hfp = 50;
    profile.rm69a10_panel.vsync = 40;
    profile.rm69a10_panel.vbp = 120;
    profile.rm69a10_panel.vfp = 80;
    profile.rm69a10_panel.lane_num = 2;
    profile.rm69a10_panel.lane_bit_rate_mbps = 1000;
    profile.rm69a10_panel.touch_address = profile.i2c.gt9895_touch;
    profile.rm69a10_panel.touch_max_x = 1060;
    profile.rm69a10_panel.touch_max_y = 2400;

    profile.boot = 35;
    profile.expander_int = 5;
    profile.lcd_backlight = 51;
    profile.default_panel = DisplayPanelType::Hi8561;
    profile.sd_enable_active_low = true;
    profile.power_3v3_active_high = false;
    profile.power_5v_active_high = true;
    profile.p4_vcca_active_high = true;
    profile.gps_wake_active_high = true;
    profile.c6_enable_active_high = true;
    profile.c6_wake_active_high = true;
    profile.c6_boot_active_low = true;
    profile.screen_reset_active_low = true;
    profile.touch_reset_active_low = true;
    profile.lora_reset_active_low = true;
    profile.lora_dio1_via_expander = true;
    profile.lora_reset_via_expander = true;
    profile.lora_rf_switch_via_expander = true;
    profile.has_display = true;
    profile.has_touch = true;
    profile.has_audio = true;
    profile.has_sdcard = true;
    profile.has_gps_uart = true;
    profile.has_lora = true;
    profile.has_c6_companion = true;
    profile.has_motion_sensor = true;
    profile.has_haptic = true;
    profile.supports_keyboard_module = true;
    profile.motion_sensor = "ICM20948";
    profile.uses_io_expander_for_lora = true;

    return profile;
}

inline constexpr BoardProfile kBoardProfile = makeBoardProfile();

} // namespace boards::t_display_p4
