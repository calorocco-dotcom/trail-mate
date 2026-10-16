#include "boards/t_display_p4/rtc_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <sys/time.h>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

namespace boards::t_display_p4::rtc_runtime
{
namespace
{

constexpr const char* kTag = "t-display-p4-rtc";
constexpr uint32_t kI2cTimeoutMs = 1000;

constexpr uint8_t kRegControl1 = 0x00;
constexpr uint8_t kRegControl2 = 0x01;
constexpr uint8_t kRegSeconds = 0x02;
constexpr uint8_t kRegMinutes = 0x03;
constexpr uint8_t kRegHours = 0x04;
constexpr uint8_t kRegDay = 0x05;
constexpr uint8_t kRegWeekday = 0x06;
constexpr uint8_t kRegMonthCentury = 0x07;
constexpr uint8_t kRegYear = 0x08;

constexpr uint8_t kSecondsVl = 1u << 7;
constexpr uint8_t kControl1Stop = 1u << 5;
constexpr uint8_t kMonthCentury = 1u << 7;

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

uint8_t bcd_to_dec(uint8_t value)
{
    if ((value & 0x0F) > 9 || (value >> 4) > 9) return UINT8_MAX;
    return static_cast<uint8_t>(((value >> 4) * 10) + (value & 0x0F));
}

uint8_t dec_to_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool epoch_to_tm_utc(std::time_t epoch_seconds, std::tm* out_tm)
{
    if (out_tm == nullptr)
    {
        return false;
    }
    return ::gmtime_r(&epoch_seconds, out_tm) != nullptr;
}

class ScopedRtcDevice
{
  public:
    ScopedRtcDevice()
    {
        const ::boards::t_display_p4::TDisplayP4Board::SystemI2cDeviceConfig config{
            "rtc",
            ::boards::t_display_p4::TDisplayP4Board::profile().i2c.rtc,
            400000,
        };
        guard_ = std::make_unique<::boards::t_display_p4::TDisplayP4Board::ManagedSystemI2cGuard>(
            ::boards::t_display_p4::TDisplayP4Board::instance(),
            config,
            kI2cTimeoutMs);
        if (!guard_ || !guard_->ok())
        {
            return;
        }
        handle_ = guard_->handle();
    }

    bool ok() const
    {
        return handle_ != nullptr;
    }

    esp_err_t read(uint8_t reg, uint8_t* out, size_t len)
    {
        if (!ok() || !out || len == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }
        return i2c_master_transmit_receive(handle_, &reg, 1, out, len, kI2cTimeoutMs);
    }

    esp_err_t write(uint8_t reg, const uint8_t* data, size_t len)
    {
        if (!ok() || !data || len == 0 || len > 16)
        {
            return ESP_ERR_INVALID_ARG;
        }
        uint8_t buffer[17] = {};
        buffer[0] = reg;
        std::memcpy(buffer + 1, data, len);
        return i2c_master_transmit(handle_, buffer, len + 1, kI2cTimeoutMs);
    }

    esp_err_t read8(uint8_t reg, uint8_t* out)
    {
        return read(reg, out, 1);
    }

    esp_err_t write8(uint8_t reg, uint8_t value)
    {
        return write(reg, &value, 1);
    }

  private:
    std::unique_ptr<::boards::t_display_p4::TDisplayP4Board::ManagedSystemI2cGuard> guard_;
    i2c_master_dev_handle_t handle_ = nullptr;
};

bool read_rtc_epoch(std::time_t* out_epoch)
{
    if (out_epoch == nullptr)
    {
        return false;
    }

    ScopedRtcDevice rtc;
    if (!rtc.ok())
    {
        return false;
    }

    uint8_t date[7] = {};
    uint8_t control1 = 0;
    if (rtc.read8(kRegControl1, &control1) != ESP_OK || (control1 & kControl1Stop) != 0)
    {
        ESP_LOGW(kTag, "PCF8563 stopped or control register unavailable; RTC time not trusted");
        return false;
    }
    if (rtc.read(kRegSeconds, date, sizeof(date)) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to read PCF8563 datetime registers");
        return false;
    }

    if ((date[0] & kSecondsVl) != 0)
    {
        ESP_LOGW(kTag, "PCF8563 low-voltage flag is set; RTC time not trusted");
        return false;
    }

    const uint8_t second = bcd_to_dec(static_cast<uint8_t>(date[0] & 0x7F));
    const uint8_t minute = bcd_to_dec(static_cast<uint8_t>(date[1] & 0x7F));
    const uint8_t hour = bcd_to_dec(static_cast<uint8_t>(date[2] & 0x3F));
    const uint8_t day = bcd_to_dec(static_cast<uint8_t>(date[3] & 0x3F));
    const uint8_t month = bcd_to_dec(static_cast<uint8_t>(date[5] & 0x1F));
    const int base_year = (date[5] & kMonthCentury) ? 1900 : 2000;
    const int year = base_year + static_cast<int>(bcd_to_dec(date[6]));

    if (!validate_datetime_utc(year, month, day, hour, minute, second))
    {
        ESP_LOGW(kTag,
                 "PCF8563 datetime invalid: %04d-%02u-%02u %02u:%02u:%02u",
                 year,
                 static_cast<unsigned>(month),
                 static_cast<unsigned>(day),
                 static_cast<unsigned>(hour),
                 static_cast<unsigned>(minute),
                 static_cast<unsigned>(second));
        return false;
    }

    const std::time_t epoch = datetime_to_epoch_utc(year, month, day, hour, minute, second);
    if (epoch < 0)
    {
        return false;
    }

    *out_epoch = epoch;
    return true;
}

bool write_rtc_epoch(std::time_t epoch_seconds, const char* source)
{
    std::tm utc_tm = {};
    if (!epoch_to_tm_utc(epoch_seconds, &utc_tm))
    {
        ESP_LOGW(kTag,
                 "Failed to expand epoch for PCF8563 write source=%s epoch=%lld",
                 source ? source : "unknown",
                 static_cast<long long>(epoch_seconds));
        return false;
    }

    const int year = utc_tm.tm_year + 1900;
    const uint8_t month = static_cast<uint8_t>(utc_tm.tm_mon + 1);
    const uint8_t day = static_cast<uint8_t>(utc_tm.tm_mday);
    const uint8_t hour = static_cast<uint8_t>(utc_tm.tm_hour);
    const uint8_t minute = static_cast<uint8_t>(utc_tm.tm_min);
    const uint8_t second = static_cast<uint8_t>(utc_tm.tm_sec);

    if (!validate_datetime_utc(year, month, day, hour, minute, second))
    {
        return false;
    }

    ScopedRtcDevice rtc;
    if (!rtc.ok())
    {
        return false;
    }

    uint8_t control1 = 0;
    if (rtc.read8(kRegControl1, &control1) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to read PCF8563 control register");
        return false;
    }

    if (rtc.write8(kRegControl1, static_cast<uint8_t>(control1 | kControl1Stop)) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to stop PCF8563 before programming time");
        return false;
    }

    const bool pre_2000 = year < 2000;
    const uint8_t payload[7] = {
        dec_to_bcd(second),
        dec_to_bcd(minute),
        dec_to_bcd(hour),
        dec_to_bcd(day),
        static_cast<uint8_t>(utc_tm.tm_wday & 0x07),
        static_cast<uint8_t>(dec_to_bcd(month) | (pre_2000 ? kMonthCentury : 0)),
        dec_to_bcd(static_cast<uint8_t>(year % 100)),
    };

    if (rtc.write(kRegSeconds, payload, sizeof(payload)) != ESP_OK)
    {
        (void)rtc.write8(kRegControl1, control1);
        ESP_LOGW(kTag, "Failed to write PCF8563 datetime payload");
        return false;
    }

    (void)rtc.write8(kRegControl2, 0x00);
    if (rtc.write8(kRegControl1, static_cast<uint8_t>(control1 & ~kControl1Stop)) != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to restart PCF8563 after programming time");
        return false;
    }

    ESP_LOGI(kTag,
             "PCF8563 updated from %s: %04d-%02u-%02u %02u:%02u:%02u UTC",
             source ? source : "unknown",
             year,
             static_cast<unsigned>(month),
             static_cast<unsigned>(day),
             static_cast<unsigned>(hour),
             static_cast<unsigned>(minute),
             static_cast<unsigned>(second));
    return true;
}

} // namespace

bool is_valid_epoch(std::time_t epoch_seconds)
{
    return epoch_seconds >= kMinValidEpochSeconds;
}

bool validate_datetime_utc(int year,
                           uint8_t month,
                           uint8_t day,
                           uint8_t hour,
                           uint8_t minute,
                           uint8_t second)
{
    if (year < 2000 || year > 2099)
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

std::time_t datetime_to_epoch_utc(int year,
                                  uint8_t month,
                                  uint8_t day,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second)
{
    if (!validate_datetime_utc(year, month, day, hour, minute, second))
    {
        return static_cast<std::time_t>(-1);
    }

    const int64_t days = days_from_civil(year, month, day);
    const int64_t sec_of_day = static_cast<int64_t>(hour) * 3600 +
                               static_cast<int64_t>(minute) * 60 +
                               static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<std::time_t>::max()))
    {
        return static_cast<std::time_t>(-1);
    }
    return static_cast<std::time_t>(epoch64);
}

bool apply_system_time(std::time_t epoch_seconds, const char* source)
{
    if (epoch_seconds < 0)
    {
        return false;
    }

    timeval tv{};
    tv.tv_sec = epoch_seconds;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        ESP_LOGW(kTag,
                 "settimeofday failed source=%s epoch=%lld",
                 source ? source : "unknown",
                 static_cast<long long>(epoch_seconds));
        return false;
    }

    ESP_LOGI(kTag,
             "System time updated from %s epoch=%lld",
             source ? source : "unknown",
             static_cast<long long>(epoch_seconds));
    return true;
}

bool apply_system_time_and_sync_rtc(std::time_t epoch_seconds, const char* source)
{
    if (!apply_system_time(epoch_seconds, source))
    {
        return false;
    }
    return write_rtc_epoch(epoch_seconds, source);
}

bool sync_system_time_from_hardware_rtc()
{
    std::time_t epoch_seconds = 0;
    if (!read_rtc_epoch(&epoch_seconds))
    {
        return false;
    }
    if (!is_valid_epoch(epoch_seconds))
    {
        return false;
    }
    return apply_system_time(epoch_seconds, "pcf8563_boot");
}

bool sync_hardware_rtc_from_system_time()
{
    return write_rtc_epoch(std::time(nullptr), "system_time");
}

} // namespace boards::t_display_p4::rtc_runtime
