#if defined(ARDUINO_T_DECK)
#include "boards/tdeck/tdeck_board.h"
#include "board/sd_utils.h"
#include "display/drivers/ST7789TDeck.h"
#include "platform/esp/arduino_common/power/battery_adc.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "platform/radio/radio_power.h"
#include "sys/bus_access_scope.h"
#include <Wire.h>
#include <ctime>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <limits>
#include <new>
#include <sys/time.h>

namespace boards::tdeck
{

namespace
{
constexpr time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 00:00:00 UTC
constexpr uint8_t kTDeckKeyboardAddress = 0x55;
constexpr uint8_t kTDeckKeyboardBrightnessCmd = 0x01;
constexpr uint8_t kTDeckKeyboardDefaultBrightnessCmd = 0x02;
constexpr uint8_t kTDeckKeyboardModeRawCmd = 0x03;
constexpr uint8_t kTDeckKeyboardModeKeyCmd = 0x04;
constexpr uint32_t kTDeckKeyboardBootDelayMs = 500;
constexpr float kTDeckRadioTcxoVoltage = 1.8f;
constexpr float kTDeckRadioCurrentLimitMa = 140.0f;
constexpr uint8_t kTDeckBacklightStepMax = 16;
constexpr uint32_t kTDeckBacklightOffDelayMs = 3;
constexpr uint32_t kTDeckBacklightWakeDelayUs = 30;
constexpr uint32_t kTDeckDisplaySpiClockMhz = 80;
constexpr uint32_t kRadioSpiLockTimeoutLogIntervalMs = 1000;
constexpr uint32_t kSharedSpiBusResource = 1;
constexpr uint32_t kSharedSpiBusOwnerId = 0x54444543U; // 'TDEC'
constexpr const char* kSharedSpiBusOwner = "tdeck_board";
// LilyGo's T-Deck GPSShield reference starts the L76K/CASIC path at 9600.
constexpr uint32_t kGpsDefaultBaud = 9600;
constexpr uint8_t kGpsProfileAuto = 0;
constexpr uint8_t kGpsProfileNmeaPassive = 1;
constexpr uint8_t kGpsProfileUbxLegacy = 2;
constexpr uint8_t kGpsProfileUbxModern = 3;
uint8_t s_backlight_level = 0;
uint32_t s_last_radio_spi_lock_timeout_log_ms = 0;
uint32_t s_suppressed_radio_spi_lock_timeout_logs = 0;

#if DEVICE_MAX_BRIGHTNESS_LEVEL > 0
constexpr uint32_t kBacklightLevelMax = DEVICE_MAX_BRIGHTNESS_LEVEL;
#else
constexpr uint32_t kBacklightLevelMax = 1;
#endif

uint8_t clamp_brightness_level(uint8_t level)
{
    return level > DEVICE_MAX_BRIGHTNESS_LEVEL ? DEVICE_MAX_BRIGHTNESS_LEVEL : level;
}

uint8_t brightness_level_to_backlight_step(uint8_t level)
{
    const uint8_t clamped = clamp_brightness_level(level);
    if (clamped == 0)
    {
        return 0;
    }

    const uint32_t scaled =
        (static_cast<uint32_t>(clamped) * kTDeckBacklightStepMax + (kBacklightLevelMax / 2U)) /
        kBacklightLevelMax;
    const uint8_t step = static_cast<uint8_t>(scaled);
    if (step == 0)
    {
        return 1;
    }
    return step > kTDeckBacklightStepMax ? kTDeckBacklightStepMax : step;
}

template <typename RadioT>
uint32_t radio_tx_timeout_ms(RadioT& radio, size_t len)
{
    const uint64_t air_us = static_cast<uint64_t>(radio.getTimeOnAir(len));
    uint64_t timeout_ms = 10ULL + ((air_us * 5ULL) + 999ULL) / 1000ULL;
    if (timeout_ms < 100ULL)
    {
        timeout_ms = 100ULL;
    }
    if (timeout_ms > 120000ULL)
    {
        timeout_ms = 120000ULL;
    }
    return static_cast<uint32_t>(timeout_ms);
}

void log_radio_spi_lock_timeout(const char* owner, TickType_t wait_ticks)
{
    const uint32_t now_ms = millis();
    ++s_suppressed_radio_spi_lock_timeout_logs;
    if (s_last_radio_spi_lock_timeout_log_ms != 0 &&
        now_ms - s_last_radio_spi_lock_timeout_log_ms <
            kRadioSpiLockTimeoutLogIntervalMs)
    {
        return;
    }
    Serial.printf("[SPI][RADIO] lock_timeout board=tdeck owner=%s wait_ticks=%lu suppressed=%lu\n",
                  owner ? owner : "-",
                  static_cast<unsigned long>(wait_ticks),
                  static_cast<unsigned long>(s_suppressed_radio_spi_lock_timeout_logs - 1));
    s_suppressed_radio_spi_lock_timeout_logs = 0;
    s_last_radio_spi_lock_timeout_log_ms = now_ms;
}

template <typename Fn>
bool withSharedSpiRadioAccess(const char* owner,
                              TickType_t wait_ticks,
                              Fn&& fn)
{
    const uint32_t wait_ms =
        static_cast<uint32_t>(wait_ticks) * portTICK_PERIOD_MS;
    sys::runtime::BusAcquireRequest request{};
    request.resource = kSharedSpiBusResource;
    request.policy = sys::runtime::BusAccessPolicy::InteractiveWorkerBounded;
    request.command_id = kSharedSpiBusOwnerId + 1;
    request.origin = kSharedSpiBusOwnerId;
    request.deadline_ms = sys::millis_now() + wait_ms;
    request.owner_label = owner ? owner : kSharedSpiBusOwner;
    sys::runtime::ScopedBusAccessToken bus_token(
        ::platform::esp::common::shared_spi_coordinator(),
        request);
    if (!bus_token.acquired())
    {
        if (wait_ticks != 0)
        {
            log_radio_spi_lock_timeout(owner, wait_ticks);
        }
        return false;
    }
    fn();
    return true;
}

void write_backlight_level(uint8_t level)
{
#if defined(DISP_BL)
    if (DISP_BL < 0)
    {
        return;
    }

    const uint8_t target = brightness_level_to_backlight_step(level);
    pinMode(DISP_BL, OUTPUT);

    // T-Deck uses the vendor backlight chip's pulse protocol rather than a raw PWM duty signal.
    if (target == 0)
    {
        digitalWrite(DISP_BL, LOW);
        delay(kTDeckBacklightOffDelayMs);
        s_backlight_level = 0;
        return;
    }

    if (s_backlight_level == 0)
    {
        digitalWrite(DISP_BL, HIGH);
        s_backlight_level = kTDeckBacklightStepMax;
        delayMicroseconds(kTDeckBacklightWakeDelayUs);
    }

    const int from = kTDeckBacklightStepMax - s_backlight_level;
    const int to = kTDeckBacklightStepMax - target;
    const int pulses = (kTDeckBacklightStepMax + to - from) % kTDeckBacklightStepMax;
    for (int i = 0; i < pulses; ++i)
    {
        digitalWrite(DISP_BL, LOW);
        digitalWrite(DISP_BL, HIGH);
    }
    s_backlight_level = target;
#else
    (void)level;
#endif
}

// Civil date to UNIX epoch days (UTC), based on Howard Hinnant's algorithm.
int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool is_leap_year(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t days_in_month(int year, uint8_t month)
{
    static constexpr uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && is_leap_year(year))
    {
        return 29;
    }
    return kDays[month - 1];
}

bool gps_datetime_valid(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 2020 || year > 2100)
    {
        return false;
    }
    if (month < 1 || month > 12)
    {
        return false;
    }
    const uint8_t max_day = days_in_month(year, month);
    if (day < 1 || day > max_day)
    {
        return false;
    }
    if (hour >= 24 || minute >= 60 || second >= 60)
    {
        return false;
    }
    return true;
}

time_t gps_datetime_to_epoch_utc(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    const int64_t days = days_from_civil(year, month, day);
    const int64_t sec_of_day = static_cast<int64_t>(hour) * 3600 +
                               static_cast<int64_t>(minute) * 60 +
                               static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<time_t>::max()))
    {
        return static_cast<time_t>(-1);
    }
    return static_cast<time_t>(epoch64);
}

int read_battery_mv_adc_fallback()
{
#ifdef BOARD_BAT_ADC
    // LilyGo T-Deck reference code uses analogReadMilliVolts(BOARD_BAT_ADC) * 2.
    return ::platform::esp::arduino_common::power::read_battery_adc_millivolts(BOARD_BAT_ADC, 2, 1);
#else
    return -1;
#endif
}

bool is_supported_gps_baud(uint32_t baud)
{
    switch (baud)
    {
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        return true;
    default:
        return false;
    }
}
} // namespace

TDeckBoard::TDeckBoard()
    : LilyGo_Display(SPI_DRIVER, false),
      LilyGoDispArduinoSPI(SCREEN_WIDTH, SCREEN_HEIGHT,
                           display::drivers::ST7789TDeck::getInitCommands(),
                           display::drivers::ST7789TDeck::getInitCommandsCount(),
                           display::drivers::ST7789TDeck::getRotationConfig(SCREEN_WIDTH, SCREEN_HEIGHT),
                           display::drivers::ST7789TDeck::getTransferConfig())
{
}

TDeckBoard* TDeckBoard::getInstance()
{
    static TDeckBoard* instance = []() -> TDeckBoard*
    {
        void* storage = heap_caps_malloc_prefer(sizeof(TDeckBoard),
                                                2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr)
        {
            storage = ::operator new(sizeof(TDeckBoard));
        }
        return new (storage) TDeckBoard();
    }();
    return instance;
}

bool TDeckBoard::initGPS()
{
    // Board init owns the physical UART. Receiver behavior/configuration is runtime-owned.
    const uint32_t manual_baud = gps_init_config_.baud;
    const bool explicit_ubx_profile = gps_init_config_.profile == kGpsProfileUbxLegacy ||
                                      gps_init_config_.profile == kGpsProfileUbxModern;
    uint32_t selected_baud = kGpsDefaultBaud;
    const char* selected_source = "legacy_default";
    const char* signature = "not_probed";
    gps::GpsReceiverProtocol protocol = gps::GpsReceiverProtocol::Unknown;

    if (is_supported_gps_baud(manual_baud))
    {
        selected_baud = manual_baud;
        selected_source = "manual_direct";
        protocol = explicit_ubx_profile ? gps::GpsReceiverProtocol::Ubx : gps::GpsReceiverProtocol::Unknown;
        signature = explicit_ubx_profile ? "ubx_profile" : "not_probed";
    }
    else if (gps_receiver_init_configured_ && gps_init_config_.profile == kGpsProfileAuto)
    {
        selected_baud = kGpsDefaultBaud;
        selected_source = "auto_direct";
    }
    else
    {
        // Keep Auto passive here; receiver-specific commands belong in runtime policy,
        // after the module/protocol has been selected.
        selected_baud = kGpsDefaultBaud;
        if (gps_init_config_.profile == kGpsProfileNmeaPassive)
        {
            protocol = gps::GpsReceiverProtocol::Nmea;
            signature = "nmea_profile";
            selected_source = "nmea_passive";
        }
        else if (gps_init_config_.profile == kGpsProfileUbxLegacy ||
                 gps_init_config_.profile == kGpsProfileUbxModern)
        {
            protocol = gps::GpsReceiverProtocol::Ubx;
            signature = "ubx_profile";
            selected_source = gps_init_config_.profile == kGpsProfileUbxLegacy ? "ubx_legacy" : "ubx_modern";
        }
    }

    Serial1.end();
    delay(10);
    Serial1.begin(selected_baud, SERIAL_8N1, GPS_RX, GPS_TX);
    const esp_err_t rx_pull_rc = gpio_set_pull_mode(static_cast<gpio_num_t>(GPS_RX), GPIO_PULLUP_ONLY);
    delay(100);
    gps_.attach(&Serial1);
    setGPSOnline(true);
    gps_receiver_protocol_ = protocol;
    Serial.printf("[TDeckBoard] GPS UART ready baud=%lu rx=%d tx=%d probe=%s signature=%s protocol=%s rx_pullup=%d\n",
                  static_cast<unsigned long>(selected_baud),
                  GPS_RX,
                  GPS_TX,
                  selected_source,
                  signature,
                  gps::gpsReceiverProtocolName(gps_receiver_protocol_),
                  rx_pull_rc == ESP_OK ? 1 : 0);
    return true;
}

void TDeckBoard::deinitGPS()
{
    Serial1.end();
    gps_receiver_protocol_ = gps::GpsReceiverProtocol::Unknown;
    setGPSOnline(false);
}

uint32_t TDeckBoard::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    boot_ms_ = millis();

    // Early probe: try to bring up serial ASAP for boot diagnostics.
    Serial.begin(115200);
    delay(20);
    Serial.println("[TDeckBoard] begin: early probe start");

    // T-Deck requires the power enable pin to be asserted very early.
#ifdef BOARD_POWERON
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    delay(50);
    Serial.println("[TDeckBoard] poweron=HIGH");
#endif
    audio_runtime_.begin();

    // Follow LilyGo T-Deck examples: de-conflict shared SPI bus early.
#ifdef SD_CS
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
#endif
#ifdef LORA_CS
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
#endif
#ifdef DISP_CS
    pinMode(DISP_CS, OUTPUT);
    digitalWrite(DISP_CS, HIGH);
#endif
#ifdef MISO
    pinMode(MISO, INPUT_PULLUP);
#endif

    // Initialize I2C early for PMU/RTC-class peripherals.
    Wire.begin(SDA, SCL);
    delay(10);

    // LilyGoDispArduinoSPI::init() establishes the shared controller inside
    // the coordinator immediately before the first display transaction.
    Serial.println("[TDeckBoard] shared SPI bus pending display initialization");

    // Initialize trackball pins early; they are active-low in LilyGo examples.
#if defined(TRACKBALL_UP) && defined(TRACKBALL_DOWN) && defined(TRACKBALL_LEFT) && defined(TRACKBALL_RIGHT)
    pinMode(TRACKBALL_UP, INPUT_PULLUP);
    pinMode(TRACKBALL_DOWN, INPUT_PULLUP);
    pinMode(TRACKBALL_LEFT, INPUT_PULLUP);
    pinMode(TRACKBALL_RIGHT, INPUT_PULLUP);
#endif
#if defined(TRACKBALL_CLICK)
    pinMode(TRACKBALL_CLICK, INPUT_PULLUP);
#endif

    // Ensure backlight rail is enabled even before full display init.
#ifdef DISP_BL
    if (DISP_BL >= 0)
    {
        pinMode(DISP_BL, OUTPUT);
        digitalWrite(DISP_BL, LOW);
        s_backlight_level = 0;
        Serial.println("[TDeckBoard] backlight held off during display init");
    }
#endif

    // Initialize radio minimally; only mark online on success.
    devices_probe_ = 0;
    pmu_ready_ = initPMU();
    touch_ready_ = initTouch();
    keyboard_ready_ = initKeyboard();
    if (keyboard_ready_)
    {
        devices_probe_ |= HW_KEYBOARD_ONLINE;
        Serial.println("[TDeckBoard] keyboard init OK");
    }
    else
    {
        Serial.println("[TDeckBoard] keyboard not detected");
    }
    rtc_ready_ = (time(nullptr) > 0);

    // Initialize display (ST7789) before SD so the SPI lock exists (pager-style ordering).
#if defined(DISP_SCK) && defined(DISP_MISO) && defined(DISP_MOSI) && defined(DISP_CS) && defined(DISP_DC)
    // Keep T-Deck display flushes fast enough that LVGL partial refresh does not visibly scan.
    display_ready_ = LilyGoDispArduinoSPI::init(DISP_SCK,
                                                DISP_MISO,
                                                DISP_MOSI,
                                                DISP_CS,
                                                DISP_RST,
                                                DISP_DC,
                                                DISP_BL,
                                                kTDeckDisplaySpiClockMhz,
                                                SPI);
    if (display_ready_)
    {
        // T-Deck default orientation should be rotated right by 90 degrees.
        LilyGoDispArduinoSPI::setRotation(1);
        rotation_ = LilyGoDispArduinoSPI::getRotation();
        setBrightness(brightness_);
        Serial.printf("[TDeckBoard] display init OK: %ux%u spi=%luMHz\n",
                      LilyGoDispArduinoSPI::_width,
                      LilyGoDispArduinoSPI::_height,
                      static_cast<unsigned long>(kTDeckDisplaySpiClockMhz));
    }
    else
    {
        Serial.println("[TDeckBoard] display init failed");
    }
#else
    Serial.println("[TDeckBoard] display init skipped: missing DISP_* pins");
#endif

    // Initialize radio before SD to align with the pager begin() sequence.
    // RadioLib configuration performs SPI traffic, so its entire hardware
    // transaction stays within the same coordinator boundary as runtime I/O.
    int radio_state = RADIOLIB_ERR_SPI_WRITE_FAILED;
    int rf_switch_state = RADIOLIB_ERR_SPI_WRITE_FAILED;
    int current_limit_state = RADIOLIB_ERR_SPI_WRITE_FAILED;
    const bool radio_bus_acquired = withSharedSpiRadioAccess(
        "radio_init",
        pdMS_TO_TICKS(200),
        [&]()
        {
            radio_.reset();
            radio_state = radio_.begin(434.0f,
                                       125.0f,
                                       9,
                                       7,
                                       RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                                       10,
                                       8,
                                       kTDeckRadioTcxoVoltage,
                                       false);
            if (radio_state == RADIOLIB_ERR_NONE)
            {
                rf_switch_state = radio_.setDio2AsRfSwitch(true);
                current_limit_state = radio_.setCurrentLimit(kTDeckRadioCurrentLimitMa);
            }
        });
    if (!radio_bus_acquired)
    {
        Serial.println("[TDeckBoard] radio init deferred: shared SPI unavailable");
    }
    else if (radio_state == RADIOLIB_ERR_NONE)
    {
        if (rf_switch_state == RADIOLIB_ERR_NONE && current_limit_state == RADIOLIB_ERR_NONE)
        {
            devices_probe_ |= HW_RADIO_ONLINE;
            Serial.println("[TDeckBoard] radio init OK");
        }
        else
        {
            Serial.printf("[TDeckBoard] radio post-init failed: rf_switch=%d current_limit=%d\n",
                          rf_switch_state,
                          current_limit_state);
        }
    }
    else
    {
        Serial.printf("[TDeckBoard] radio init failed: %d\n", radio_state);
    }

    // Initialize SD card - optional, with retry (pager-style ordering).
    if ((disable_hw_init & NO_HW_SD) == 0)
    {
        const int max_retries = 2;
        sd_ready_ = false;
        for (int retry = 0; retry < max_retries; ++retry)
        {
            if (installSD())
            {
                sd_ready_ = true;
                devices_probe_ |= HW_SD_ONLINE;
                break;
            }
            if (retry < max_retries - 1)
            {
                Serial.printf("[TDeckBoard] SD init failed, retrying... (%d/%d)\n", retry + 1, max_retries);
                delay(100);
            }
            else
            {
                Serial.printf("[TDeckBoard] SD init failed after %d attempts\n", max_retries);
            }
        }
    }
    else
    {
        Serial.println("[TDeckBoard] SD init skipped by NO_HW_SD");
    }

    if ((disable_hw_init & NO_HW_GPS) == 0)
    {
        (void)initGPS();
    }
    else
    {
        Serial.println("[TDeckBoard] GPS init skipped by NO_HW_GPS");
    }
    Serial.println("[TDeckBoard] begin: early probe done");
    return devices_probe_;
}

bool TDeckBoard::initPMU()
{
    // T-Deck commonly ships with AXP2101 at 0x34, but I2C pins can differ by revision.
    bool ok = pmu_.begin(Wire, AXP2101_SLAVE_ADDRESS, SDA, SCL);
    if (!ok)
    {
#if defined(SENSOR_SDA) && defined(SENSOR_SCL)
        if (SENSOR_SDA != SDA || SENSOR_SCL != SCL)
        {
            Wire.begin(SENSOR_SDA, SENSOR_SCL);
            delay(10);
            ok = pmu_.begin(Wire, AXP2101_SLAVE_ADDRESS, SENSOR_SDA, SENSOR_SCL);
        }
#endif
        // Restore the primary bus for the rest of the system.
        Wire.begin(SDA, SCL);
        delay(5);
    }

    if (ok)
    {
        // Ensure PMU battery metrics are enabled; otherwise SOC can report stale 0.
        pmu_.enableBattDetection();
        pmu_.enableVbusVoltageMeasure();
        pmu_.enableBattVoltageMeasure();
        pmu_.enableSystemVoltageMeasure();
        pmu_.enableTemperatureMeasure();
    }
    Serial.printf("[TDeckBoard] PMU init: %s\n", ok ? "OK" : "FAIL");
    return ok;
}

bool TDeckBoard::initTouch()
{
#if defined(BOARD_TOUCH_INT)
    touch_.setPins(-1, BOARD_TOUCH_INT);
#else
    touch_.setPins(-1, -1);
#endif

    bool ok = touch_.begin(Wire, GT911_SLAVE_ADDRESS_H, SDA, SCL);
    if (!ok)
    {
        ok = touch_.begin(Wire, GT911_SLAVE_ADDRESS_L, SDA, SCL);
    }
    if (!ok)
    {
        Serial.println("[TDeckBoard] touch init failed");
        return false;
    }

    // Align with LilyGo T-Deck reference touch mapping.
    touch_.setMaxCoordinates(SCREEN_WIDTH, SCREEN_HEIGHT);
    touch_.setSwapXY(true);
    touch_.setMirrorXY(false, true);
    Serial.println("[TDeckBoard] touch init OK");
    return true;
}

bool TDeckBoard::initKeyboard()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    const uint32_t elapsed = millis() - boot_ms_;
    if (elapsed < kTDeckKeyboardBootDelayMs)
    {
        delay(kTDeckKeyboardBootDelayMs - elapsed);
    }

    Wire.beginTransmission(kTDeckKeyboardAddress);
    if (Wire.endTransmission() != 0)
    {
        Serial.println("[TDeckBoard] keyboard probe failed");
        return false;
    }

    if (!sendKeyboardCommand(kTDeckKeyboardModeKeyCmd))
    {
        Serial.println("[TDeckBoard] keyboard key-mode command failed");
        return false;
    }

    const int value = readKeyboardByte();
    if (value < 0)
    {
        return false;
    }

    keyboard_pending_release_ = false;
    keyboard_last_char_ = '\0';
    const uint8_t default_brightness = keyboard_brightness_ > 0 ? keyboard_brightness_ : 127;
    sendKeyboardCommand(kTDeckKeyboardDefaultBrightnessCmd, default_brightness);
    sendKeyboardCommand(kTDeckKeyboardBrightnessCmd, keyboard_brightness_);
    return true;
#else
    return false;
#endif
}

bool TDeckBoard::sendKeyboardCommand(uint8_t cmd)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    Wire.beginTransmission(kTDeckKeyboardAddress);
    Wire.write(cmd);
    return Wire.endTransmission() == 0;
#else
    (void)cmd;
    return false;
#endif
}

bool TDeckBoard::sendKeyboardCommand(uint8_t cmd, uint8_t value)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    Wire.beginTransmission(kTDeckKeyboardAddress);
    Wire.write(cmd);
    Wire.write(value);
    return Wire.endTransmission() == 0;
#else
    (void)cmd;
    (void)value;
    return false;
#endif
}

int TDeckBoard::readKeyboardByte()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    const uint8_t bytes = Wire.requestFrom(kTDeckKeyboardAddress, static_cast<uint8_t>(1));
    if (bytes < 1 || Wire.available() < 1)
    {
        return -1;
    }
    const int value = Wire.read();
    while (Wire.available() > 0)
    {
        Wire.read();
    }
    return value;
#else
    return -1;
#endif
}

char TDeckBoard::translateKeyboardByte(uint8_t value)
{
    if (value == 0x00)
    {
        return '\0';
    }
    if (value == 0x08)
    {
        return '\b';
    }
    if (value == 0x0D || value == 0x0A)
    {
        return '\n';
    }
    return static_cast<char>(value);
}

bool TDeckBoard::installSD()
{
    if (!::platform::esp::boards::storageStartupGateSatisfied())
    {
        Serial.println("[TDeckBoard] SD mount deferred: first shared-SPI display transaction incomplete");
        return false;
    }

#ifdef SD_CS
    const int* extra_cs = nullptr;
    size_t extra_cs_count = 0;
#if defined(LORA_CS) && defined(DISP_CS)
    static const int extra_cs_pins[] = {LORA_CS, DISP_CS};
    extra_cs = extra_cs_pins;
    extra_cs_count = sizeof(extra_cs_pins) / sizeof(extra_cs_pins[0]);
#elif defined(LORA_CS)
    static const int extra_cs_pins[] = {LORA_CS};
    extra_cs = extra_cs_pins;
    extra_cs_count = 1;
#elif defined(DISP_CS)
    static const int extra_cs_pins[] = {DISP_CS};
    extra_cs = extra_cs_pins;
    extra_cs_count = 1;
#endif

    uint8_t cardType = sdutil::kCardNone;
    uint32_t cardSizeMB = 0;
    bool ok = sdutil::installSpiSd(SD_CS, SD_SPI_FREQUENCY, "/sd",
                                   extra_cs, extra_cs_count,
                                   &cardType, &cardSizeMB);

    Serial.printf("[TDeckBoard] SD init: %s\n", ok ? "OK" : "FAIL");
    if (ok)
    {
        Serial.printf("[TDeckBoard] SD card type=%u size=%luMB\n",
                      (unsigned)cardType, (unsigned long)cardSizeMB);
    }
    return ok;
#else
    Serial.println("[TDeckBoard] SD init skipped: missing SD_CS");
    return false;
#endif
}

bool TDeckBoard::ensureSDReady()
{
    if (isCardReady())
    {
        sd_ready_ = true;
        devices_probe_ |= HW_SD_ONLINE;
        return true;
    }

    sd_ready_ = installSD();
    if (sd_ready_)
    {
        devices_probe_ |= HW_SD_ONLINE;
    }
    else
    {
        devices_probe_ &= ~HW_SD_ONLINE;
    }
    return sd_ready_;
}

void TDeckBoard::uninstallSD()
{
    ::platform::esp::arduino_common::storage::unmount_sd_card();
    Serial.println("[TDeckBoard] SD unmounted");
}

bool TDeckBoard::isRTCReady() const
{
    // T-Deck has no dedicated external RTC chip in this project. We treat
    // "RTC ready" as "system epoch has been set to a sane value".
    const time_t now = time(nullptr);
    return rtc_ready_ || (now >= kMinValidEpochSeconds);
}

bool TDeckBoard::isCharging()
{
    if (!pmu_ready_)
    {
        return false;
    }
    return pmu_.isCharging();
}

int TDeckBoard::getBatteryLevel()
{
    power::BatteryEstimatorSample sample{};
    sample.now_ms = millis();
    sample.adc_battery_mv = read_battery_mv_adc_fallback();

    if (pmu_ready_)
    {
        // AXP2101 percent can jump by board/revision. T-Deck follows the LilyGo
        // ADC divider path for display, with PMU voltage only as fallback.
        if (sample.adc_battery_mv <= 0)
        {
            sample.pmu_battery_mv = static_cast<int>(pmu_.getBattVoltage());
        }
        sample.charging = pmu_.isCharging();
        sample.vbus_present = pmu_.isVbusIn();
    }

    const power::BatteryEstimate estimate = battery_estimator_.update(sample);
    return estimate.percent;
}

void TDeckBoard::setBrightness(uint8_t level)
{
    const uint8_t clamped_level = clamp_brightness_level(level);
    brightness_ = clamped_level;
    if (!display_ready_)
    {
        return;
    }

    LilyGoDispArduinoSPI::setBrightness(clamped_level);
#if defined(DISP_BL)
    if (DISP_BL >= 0)
    {
        write_backlight_level(clamped_level);
    }
#endif
}

bool TDeckBoard::hasKeyboard()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    return (devices_probe_ & HW_KEYBOARD_ONLINE) != 0;
#else
    return false;
#endif
}

void TDeckBoard::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = level;
#ifdef USING_INPUT_DEV_KEYBOARD
    if (keyboard_ready_)
    {
        sendKeyboardCommand(kTDeckKeyboardBrightnessCmd, level);
    }
#endif
}

uint8_t TDeckBoard::keyboardGetBrightness()
{
    return keyboard_brightness_;
}

bool TDeckBoard::isCardReady()
{
    return ::platform::esp::arduino_common::storage::sd_card_ready();
}

void TDeckBoard::setRotation(uint8_t rotation)
{
    LilyGoDispArduinoSPI::setRotation(rotation);
    rotation_ = LilyGoDispArduinoSPI::getRotation();
}

uint8_t TDeckBoard::getRotation()
{
    return LilyGoDispArduinoSPI::getRotation();
}

void TDeckBoard::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    LilyGoDispArduinoSPI::pushColors(x1, y1, x2, y2, color);
}

DisplayTransferResult TDeckBoard::transferPixels(uint16_t x1,
                                                 uint16_t y1,
                                                 uint16_t x2,
                                                 uint16_t y2,
                                                 uint16_t* color)
{
    return LilyGoDispArduinoSPI::transferPixels(x1, y1, x2, y2, color);
}

bool TDeckBoard::pushColorsResult(uint16_t x1,
                                  uint16_t y1,
                                  uint16_t x2,
                                  uint16_t y2,
                                  uint16_t* color)
{
    return LilyGoDispArduinoSPI::pushColorsResult(x1, y1, x2, y2, color);
}

uint16_t TDeckBoard::width()
{
    return LilyGoDispArduinoSPI::_width;
}

uint16_t TDeckBoard::height()
{
    return LilyGoDispArduinoSPI::_height;
}

uint8_t TDeckBoard::getPoint(int16_t* x, int16_t* y, uint8_t get_point)
{
    if (!touch_ready_)
    {
        return 0;
    }

    uint8_t touched = touch_.getPoint(x, y, get_point);
    if (!touched || !x || !y)
    {
        return touched;
    }

    int16_t tx = *x;
    int16_t ty = *y;
    const int16_t w = static_cast<int16_t>(SCREEN_WIDTH);
    const int16_t h = static_cast<int16_t>(SCREEN_HEIGHT);
    if (tx < 0)
    {
        tx = 0;
    }
    else if (tx >= w)
    {
        tx = static_cast<int16_t>(w - 1);
    }
    if (ty < 0)
    {
        ty = 0;
    }
    else if (ty >= h)
    {
        ty = static_cast<int16_t>(h - 1);
    }

    // Map raw touch coordinates into the current display rotation.
    // This keeps the logical LVGL coordinates aligned with what the user sees.
    int16_t lx = tx;
    int16_t ly = ty;
    switch (rotation_)
    {
    case 0:
        // No rotation: direct mapping.
        break;
    case 1:
        // Rotato di 90 gradi (default del T-Deck). setSwapXY(true)/setMirrorXY(false, true)
        // in initTouch() gia' convertono le coordinate grezze del pannello nell'orientamento
        // landscape corretto: un secondo rimappaggio qui le ribaltava di nuovo (il tocco
        // risultava registrato sul lato opposto a quello toccato). Passthrough diretto.
        break;
    case 2:
        // Rotated 180 degrees.
        lx = static_cast<int16_t>(w - 1 - tx);
        ly = static_cast<int16_t>(h - 1 - ty);
        break;
    case 3:
        // Rotated 270 degrees.
        lx = static_cast<int16_t>(h - 1 - ty);
        ly = tx;
        break;
    default:
        break;
    }

    *x = lx;
    *y = ly;
    return touched;
}

int TDeckBoard::getKeyChar(char* c)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    if (!keyboard_ready_ || !c)
    {
        return -1;
    }

    if (keyboard_pending_release_)
    {
        *c = keyboard_last_char_;
        keyboard_pending_release_ = false;
        return KEYBOARD_RELEASED;
    }

    const int value = readKeyboardByte();
    if (value < 0)
    {
        return -1;
    }

    const char translated = translateKeyboardByte(static_cast<uint8_t>(value));
    if (translated == '\0')
    {
        return -1;
    }

    *c = translated;
    keyboard_last_char_ = translated;
    keyboard_pending_release_ = true;
    return KEYBOARD_PRESSED;
#else
    (void)c;
    return -1;
#endif
}

bool TDeckBoard::syncTimeFromGPS(uint32_t gps_task_interval_ms)
{
    if (!gps_.date.isValid() || !gps_.time.isValid())
    {
        return false;
    }

    uint16_t year = gps_.date.year();
    uint8_t month = gps_.date.month();
    uint8_t day = gps_.date.day();
    uint8_t hour = gps_.time.hour();
    uint8_t minute = gps_.time.minute();
    uint8_t second = gps_.time.second();

    if (!gps_datetime_valid(year, month, day, hour, minute, second))
    {
        Serial.printf("[TDeckBoard] GPS time rejected: %04u-%02u-%02u %02u:%02u:%02u\n",
                      year, month, day, hour, minute, second);
        return false;
    }

    // NMEA timestamps are typically not "now"; apply a conservative compensation.
    const uint32_t read_start_ms = millis();
    uint32_t task_interval_comp_ms = 0;
    if (gps_task_interval_ms > 5000)
    {
        task_interval_comp_ms = gps_task_interval_ms / 2;
        if (task_interval_comp_ms > 5000)
        {
            task_interval_comp_ms = 5000;
        }
    }
    const uint32_t processing_delay_ms = millis() - read_start_ms;
    const uint32_t total_delay_ms = 2000 + task_interval_comp_ms + processing_delay_ms;

    time_t epoch = gps_datetime_to_epoch_utc(year, month, day, hour, minute, second);
    if (epoch < kMinValidEpochSeconds)
    {
        return false;
    }
    epoch += static_cast<time_t>((total_delay_ms + 500) / 1000);

    timeval tv{};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        Serial.println("[TDeckBoard] settimeofday() failed");
        return false;
    }

    rtc_ready_ = true;
    Serial.printf("[TDeckBoard] Time synced from GPS: %04u-%02u-%02u %02u:%02u:%02u (sat=%u fix=%d)\n",
                  year, month, day, hour, minute, second,
                  static_cast<unsigned>(gps_.satellites.value()),
                  gps_.location.isValid() ? 1 : 0);
    return true;
}

int TDeckBoard::transmitRadio(const uint8_t* data, size_t len)
{
    if (!platform::radio::power_policy().enabled()) return RADIOLIB_ERR_SPI_WRITE_FAILED;
    const uint32_t timeout_ms = radio_tx_timeout_ms(radio_, len);
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_tx", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.startTransmit(data, len); }))
    {
        if (rc != RADIOLIB_ERR_NONE)
        {
            return rc;
        }
    }
    else
    {
        return RADIOLIB_ERR_SPI_WRITE_FAILED;
    }

    const uint32_t started_ms = millis();
    while (digitalRead(LORA_IRQ) == LOW)
    {
        if (static_cast<uint32_t>(millis() - started_ms) > timeout_ms)
        {
            (void)withSharedSpiRadioAccess("radio_tx_finish",
                                           pdMS_TO_TICKS(50),
                                           [&]()
                                           { (void)radio_.finishTransmit(); });
            return RADIOLIB_ERR_TX_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (withSharedSpiRadioAccess("radio_tx_finish", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.finishTransmit(); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TDeckBoard::startRadioReceive()
{
    if (!platform::radio::power_policy().enabled()) return RADIOLIB_ERR_SPI_WRITE_FAILED;
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_rx", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.startReceive(); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

bool TDeckBoard::quiesceForExternalStorage()
{
    if (!isRadioOnline())
    {
        return true;
    }

    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    return withSharedSpiRadioAccess("radio_external_storage_standby",
                                    pdMS_TO_TICKS(50),
                                    [&]()
                                    { rc = radio_.standby(); }) &&
           rc == RADIOLIB_ERR_NONE;
}

uint32_t TDeckBoard::getRadioIrqFlags()
{
    uint32_t flags = 0;
    if (withSharedSpiRadioAccess("radio_irq", 0, [&]()
                                 { flags = radio_.getIrqFlags(); }))
    {
        return flags;
    }
    return 0;
}

int TDeckBoard::getRadioPacketLength(bool update)
{
    int len = 0;
    if (withSharedSpiRadioAccess("radio_rx", pdMS_TO_TICKS(20), [&]()
                                 { len = static_cast<int>(radio_.getPacketLength(update)); }))
    {
        return len;
    }
    return 0;
}

int TDeckBoard::readRadioData(uint8_t* buf, size_t len)
{
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_rx", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.readData(buf, len); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

void TDeckBoard::clearRadioIrqFlags(uint32_t flags)
{
    (void)withSharedSpiRadioAccess("radio_irq", pdMS_TO_TICKS(20), [&]()
                                   { radio_.clearIrqFlags(flags); });
}

float TDeckBoard::getRadioRSSI()
{
    float rssi = std::numeric_limits<float>::quiet_NaN();
    if (withSharedSpiRadioAccess("radio_rssi", pdMS_TO_TICKS(20), [&]()
                                 { rssi = radio_.getRSSI(); }))
    {
        return rssi;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

float TDeckBoard::getRadioInstantRSSI()
{
    float rssi = std::numeric_limits<float>::quiet_NaN();
    if (withSharedSpiRadioAccess("radio_rssi", pdMS_TO_TICKS(20), [&]()
                                 { rssi = radio_.getRSSI(false); }))
    {
        return rssi;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

float TDeckBoard::getRadioSNR()
{
    float snr = std::numeric_limits<float>::quiet_NaN();
    if (withSharedSpiRadioAccess("radio_rssi", pdMS_TO_TICKS(20), [&]()
                                 { snr = radio_.getSNR(); }))
    {
        return snr;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

#if defined(ARDUINO_LILYGO_LORA_SX1262)
static void apply_tx_power(SX1262Access& radio, int8_t tx_power)
{
    constexpr int8_t kTxPowerMinDbm = -9;
    int8_t clipped = tx_power;
    if (clipped < kTxPowerMinDbm) clipped = kTxPowerMinDbm;
    radio.setOutputPower(clipped);
}
#endif

int TDeckBoard::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                   int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                   uint8_t crc_len)
{
    int first_error = RADIOLIB_ERR_NONE;
    const bool configured = withSharedSpiRadioAccess("radio_cfg", pdMS_TO_TICKS(100), [&]()
                                                     {
        const auto note_error = [&first_error](int rc)
        {
            if (rc != RADIOLIB_ERR_NONE && first_error == RADIOLIB_ERR_NONE)
            {
                first_error = rc;
            }
        };
#if defined(ARDUINO_LILYGO_LORA_SX1262)
        note_error(radio_.setDio2AsRfSwitch(true));
        note_error(radio_.setCurrentLimit(kTDeckRadioCurrentLimitMa));
#endif
        note_error(radio_.setFrequency(freq_mhz));
        note_error(radio_.setBandwidth(bw_khz));
        note_error(radio_.setSpreadingFactor(sf));
        note_error(radio_.setCodingRate(cr_denom));
#if defined(ARDUINO_LILYGO_LORA_SX1262)
        apply_tx_power(radio_, tx_power);
#else
        note_error(radio_.setOutputPower(tx_power));
#endif
        note_error(radio_.setPreambleLength(preamble_len));
        note_error(radio_.setSyncWord(sync_word));
        note_error(radio_.setCRC(crc_len)); });
    return configured ? first_error : RADIOLIB_ERR_SPI_WRITE_FAILED;
}

// Torna al T-Deck-Launcher (pressione lunga del click della trackball), se questo .bin e'
// stato avviato da li': il launcher scrive i programmi letti dalla SD nella partizione
// "ota_0" e tiene sempre "factory" libera per se stesso (vedi T-Deck-Launcher/partitions_
// launcher.csv e src/launcher_api.h). Se il firmware e' stato flashato standalone (senza
// quella tabella di partizioni) "factory" non esiste con questo nome ed esp_partition_
// find_first ritorna nullptr: la funzione non fa nulla, innocua in entrambi i casi.
static void returnToLauncherIfRequested()
{
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
    if (!factory)
    {
        return;
    }
    esp_ota_set_boot_partition(factory);
    esp_restart();
}

RotaryMsg_t TDeckBoard::getRotary()
{
    // T-Deck trackball is modeled as directional keypad input so the menu can
    // move in two dimensions. Leave encoder output disabled on this board.
    RotaryMsg_t msg{};
    return msg;
}

int TDeckBoard::getNavKey(uint32_t* key)
{
    if (!key)
    {
        return -1;
    }

    static bool up_state = false;
    static bool down_state = false;
    static bool left_state = false;
    static bool right_state = false;
    static bool click_state = false;
    static bool click_consumed = false;
    static bool pending_release = false;
    static uint32_t pending_key = INPUT_NAV_KEY_NONE;
    static uint32_t up_change_ms = 0;
    static uint32_t down_change_ms = 0;
    static uint32_t left_change_ms = 0;
    static uint32_t right_change_ms = 0;
    static uint32_t click_change_ms = 0;

    if (pending_release)
    {
        *key = pending_key;
        pending_release = false;
        return KEYBOARD_RELEASED;
    }

    const uint32_t now = millis();
    if ((now - boot_ms_) < kRotaryBootGuardMs)
    {
        // Ignore early boot noise from the trackball/boot pins.
        return -1;
    }

    // T-Deck trackball tuning:
    // - Use press-edge pulse detection to avoid mixed/sticky level states.
    // - Map physical directions directly to logical UI directions.
    const uint32_t repeat_ms = 110;  // Minimum spacing between direction events
    const uint32_t click_ms = 150;   // Click debounce
    const uint32_t debounce_ms = 22; // Require stable press/release

    bool up_pressed = false;
    bool down_pressed = false;
    bool left_pressed = false;
    bool right_pressed = false;
#if defined(TRACKBALL_RIGHT) && defined(TRACKBALL_LEFT)
    right_pressed = (digitalRead(TRACKBALL_RIGHT) == LOW);
    left_pressed = (digitalRead(TRACKBALL_LEFT) == LOW);
#endif
#if defined(TRACKBALL_UP) && defined(TRACKBALL_DOWN)
    up_pressed = (digitalRead(TRACKBALL_UP) == LOW);
    down_pressed = (digitalRead(TRACKBALL_DOWN) == LOW);
#endif

    auto detect_press_edge = [&](bool pressed, bool& state, uint32_t& change_ms) -> bool
    {
        if (pressed != state)
        {
            state = pressed;
            if (pressed && (now - change_ms) >= debounce_ms)
            {
                change_ms = now;
                return true;
            }
            change_ms = now;
        }
        return false;
    };

    const bool up_edge = detect_press_edge(up_pressed, up_state, up_change_ms);
    const bool down_edge = detect_press_edge(down_pressed, down_state, down_change_ms);
    const bool left_edge = detect_press_edge(left_pressed, left_state, left_change_ms);
    const bool right_edge = detect_press_edge(right_pressed, right_state, right_change_ms);

    uint32_t nav_key = INPUT_NAV_KEY_NONE;
    if ((now - last_trackball_ms_) >= repeat_ms)
    {
        if (left_edge)
        {
            nav_key = INPUT_NAV_KEY_LEFT;
            last_trackball_ms_ = now;
        }
        else if (right_edge)
        {
            nav_key = INPUT_NAV_KEY_RIGHT;
            last_trackball_ms_ = now;
        }
        else if (up_edge)
        {
            nav_key = INPUT_NAV_KEY_UP;
            last_trackball_ms_ = now;
        }
        else if (down_edge)
        {
            nav_key = INPUT_NAV_KEY_DOWN;
            last_trackball_ms_ = now;
        }
    }

#if defined(TRACKBALL_CLICK)
    const uint32_t kLauncherHoldMs = 1500;   // Pressione lunga: torna al T-Deck-Launcher
    static bool launcher_return_armed = false;
    const bool click_pressed = digitalRead(TRACKBALL_CLICK) == LOW;
    if (click_pressed != click_state)
    {
        click_state = click_pressed;
        click_change_ms = now;
        if (click_pressed)
        {
            launcher_return_armed = true;   // Nuova pressione: riarma il controllo della durata
        }
    }

    // Re-arm click detection after a stable release, so every press can emit one event.
    if (!click_state && (now - click_change_ms) >= debounce_ms)
    {
        click_consumed = false;
    }

    // Tenuto premuto abbastanza a lungo (indipendentemente dal normale click->ENTER, gia'
    // emesso subito dopo il debounce): esce dall'app e torna al launcher. Non ha effetto se
    // il firmware non e' stato flashato con le partizioni del launcher (vedi funzione sopra).
    if (click_state && launcher_return_armed && (now - click_change_ms) >= kLauncherHoldMs)
    {
        launcher_return_armed = false;
        returnToLauncherIfRequested();
    }

    if (click_state && (now - click_change_ms) >= debounce_ms &&
        !click_consumed && (now - last_click_ms_) >= click_ms)
    {
        nav_key = INPUT_NAV_KEY_ENTER;
        last_click_ms_ = now;
        click_consumed = true;
    }
#endif

    if (nav_key == INPUT_NAV_KEY_NONE)
    {
        return -1;
    }

    pending_key = nav_key;
    pending_release = true;
    *key = nav_key;
    return KEYBOARD_PRESSED;
}

void TDeckBoard::playMessageTone()
{
#if defined(DAC_I2S_BCK) && defined(DAC_I2S_WS) && defined(DAC_I2S_DOUT)
    std::printf("[TDeck][Audio] message tone requested volume=%u\n",
                static_cast<unsigned>(message_tone_volume_));
    audio_runtime_.requestMessageTone(message_tone_volume_);
#endif
}

void TDeckBoard::setMessageToneVolume(uint8_t volume_percent)
{
    if (volume_percent > 100)
    {
        volume_percent = 100;
    }
    message_tone_volume_ = volume_percent;
}

uint8_t TDeckBoard::getMessageToneVolume() const
{
    return message_tone_volume_;
}

bool TDeckBoard::isVoicePlaybackReady() const
{
    return audio_runtime_.isReady();
}

bool TDeckBoard::playCodec2Voice(const uint8_t* encoded_media,
                                 std::size_t encoded_media_len,
                                 uint8_t volume_percent)
{
    return audio_runtime_.playCodec2Voice(encoded_media, encoded_media_len, volume_percent);
}

namespace
{
TDeckBoard& getInstanceRef()
{
    return *TDeckBoard::getInstance();
}
} // namespace

TDeckBoard& instance = getInstanceRef();
BoardBase& board = getInstanceRef();

} // namespace boards::tdeck

BoardBase& board = ::boards::tdeck::instance;
#endif // defined(ARDUINO_T_DECK)
