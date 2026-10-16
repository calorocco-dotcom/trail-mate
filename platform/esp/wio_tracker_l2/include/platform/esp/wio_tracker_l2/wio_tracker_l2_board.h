#pragma once

#include "board/BoardBase.h"
#include "board/GpsBoard.h"
#include "board/LoraBoard.h"
#include "board/SdBoard.h"
#include "boards/wio_tracker_l2/board_profile.h"
#include "display/DisplayInterface.h"
#include "platform/esp/arduino_common/gps/GPS.h"

#include <RadioLib.h>

namespace boards::wio_tracker_l2
{

class WioTrackerL2Board final : public BoardBase,
                                public GpsBoard,
                                public LoraBoard,
                                public SdBoard,
                                public LilyGo_Display
{
  public:
    static WioTrackerL2Board& instance();
    uint32_t begin(uint32_t disable_hw_init = 0) override;
    void wakeUp() override;
    void handlePowerButton() override;
    void softwareShutdown() override;
    void enterScreenSleep() override;
    void exitScreenSleep() override;
    void setBrightness(uint8_t level) override;
    uint8_t getBrightness() override { return brightness_; }
    bool hasKeyboard() override { return false; }
    void keyboardSetBrightness(uint8_t) override {}
    uint8_t keyboardGetBrightness() override { return 0; }
    bool isRTCReady() const override;
    bool isCharging() override;
    int getBatteryLevel() override;
    bool isSDReady() const override;
    bool isCardReady() override;
    bool isGPSReady() const override { return gps_ready_; }
    bool hasGPSHardware() const override { return true; }
    void vibrator() override {}
    void stopVibrator() override {}
    void playMessageTone() override;
    void setMessageToneVolume(uint8_t level) override;
    uint8_t getMessageToneVolume() const override { return tone_volume_; }
    bool isVoicePlaybackReady() const { return audio_ready_; }
    bool beginMusic(uint32_t sample_rate);
    bool writeMusic(const int16_t* mono, size_t samples);
    void endMusic();
    bool playCodec2Voice(const uint8_t* data, size_t size, uint8_t volume);

    bool installSD() override;
    bool ensureSDReady() override;
    void uninstallSD() override;

    void setGPSReceiverInitConfig(const gps::GpsReceiverInitConfig& config) override
    {
        gps_config_ = config;
        gps_config_.profile = 1;
        gps_config_.rxm_policy = 1;
        gps_config_.gnss_policy = 1;
        gps_config_.nmea_policy = 1;

        if (gps_config_.baud == 0)
        {
            gps_config_.baud = 9600;
        }
    }
    gps::GpsReceiverProtocol getGPSReceiverProtocol() const override { return gps::GpsReceiverProtocol::Nmea; }
    bool initGPS() override;
    void deinitGPS() override;
    void setGPSOnline(bool online) override { gps_ready_ = online; }
    GPS& getGPS() override { return gps_; }
    void powerControl(PowerCtrlChannel_t channel, bool enabled) override;
    bool syncTimeFromGPS(uint32_t gps_task_interval_ms = 0) override;

    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override { return rotation_; }
    uint16_t width() override { return rotation_ & 1U ? kPanelWidth : kPanelHeight; }
    uint16_t height() override { return rotation_ & 1U ? kPanelHeight : kPanelWidth; }
    bool hasTouch() override { return touch_ready_; }
    // LovyanGFX owns its transport buffers; keep the large LVGL buffers in PSRAM.
    bool useDMA() override { return false; }
    uint8_t getPoint(int16_t* x, int16_t* y, uint8_t count) override;
    void pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t* pixels) override;
    DisplayTransferResult transferPixels(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t* pixels) override;

    bool isRadioOnline() const override { return radio_ready_; }
    int transmitRadio(const uint8_t* data, size_t len) override;
    int startRadioReceive() override;
    uint32_t getRadioIrqFlags() override;
    int getRadioPacketLength(bool update) override;
    int readRadioData(uint8_t* buffer, size_t len) override;
    void clearRadioIrqFlags(uint32_t flags) override;
    float getRadioRSSI() override;
    float getRadioInstantRSSI() override;
    float getRadioSNR() override;
    int configureLoraRadio(float frequency, float bandwidth, uint8_t sf, uint8_t cr,
                           int8_t power, uint16_t preamble, uint8_t sync_word, uint8_t crc) override;
    bool quiesceForExternalStorage() override;

  private:
    WioTrackerL2Board();
    bool initializePower();
    bool initializeDisplay();
    bool initializeBacklight();
    bool initializeAudio();
    bool ensureAudioReady();
    void playTone();
    bool writeExpander(ExpanderPin pin, bool high);
    void writeBacklight(uint8_t level);

    SPIClass radio_spi_;
    Module radio_module_;
    SX1262 radio_;
    GPS gps_;
    gps::GpsReceiverInitConfig gps_config_{};
    uint16_t output_latch_ = 0;
    uint16_t direction_mask_ = 0xFFFF;
    uint8_t brightness_ = 8;
    uint8_t tone_volume_ = 45;
    uint8_t rotation_ = 0;
    bool started_ = false;
    bool expander_ready_ = false;
    bool display_ready_ = false;
    bool backlight_ready_ = false;
    bool touch_ready_ = false;
    uint8_t touch_address_ = i2c::kTouchAddress;
    bool touch_pressed_ = false;
    int16_t touch_x_ = 0;
    int16_t touch_y_ = 0;
    uint32_t touch_sample_ms_ = 0;
    bool radio_ready_ = false;
    bool gps_ready_ = false;
    bool time_ready_ = false;
    bool audio_ready_ = false;
    bool screen_sleeping_ = false;
    bool wake_pressed_ = false;
    uint32_t wake_pressed_ms_ = 0;
    uint32_t button_poll_ms_ = 0;
    uint32_t battery_read_ms_ = 0;
    int battery_level_ = -1;
};

} // namespace boards::wio_tracker_l2
