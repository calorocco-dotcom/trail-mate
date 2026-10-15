#include "platform/esp/idf_common/gps_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <mutex>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "boards/tab5/tab5_board.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/ui/device_runtime.h"

namespace platform::esp::idf_common::gps_runtime
{
namespace
{
constexpr const char* kTag = "idf-gps-runtime";
constexpr size_t kMaxFields = 24;
constexpr size_t kLineBufferSize = 160;
constexpr size_t kRxBufferSize = 512;
constexpr std::time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC
constexpr uint32_t kProbeWarmupMs = 1500;
constexpr uint32_t kProbeListenMs = 3500;
constexpr uint32_t kNoDataWarnMs = 5000;
constexpr size_t kRawDiagnosticSampleBytes = 16;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
constexpr uint32_t kP4GnssFallbackBaud = 9600;
constexpr uint32_t kP4GnssFallbackWakeCycleMs = 100;
#endif

enum class NmeaParseResult : uint8_t
{
    Accepted = 0,
    Unsupported,
    MissingDollar,
    InvalidChecksum,
    Malformed,
};

struct GpsRxDiagnostics
{
    uint64_t discarded_prefix_bytes = 0;
    uint32_t completed_lines = 0;
    uint32_t accepted_sentences = 0;
    uint32_t unsupported_sentences = 0;
    uint32_t invalid_checksums = 0;
    uint32_t malformed_sentences = 0;
    uint32_t line_overflows = 0;
    std::array<uint8_t, kRawDiagnosticSampleBytes> raw_sample{};
    size_t raw_sample_len = 0;
};

enum class CollectorSlot : size_t
{
    GPS = 0,
    GLN,
    GAL,
    BD,
    UNKNOWN,
    COUNT
};

struct GsvCollector
{
    std::array<gps::GnssSatInfo, gps::kMaxGnssSats> sats{};
    std::size_t count = 0;
};

struct RuntimeState
{
    gps::GpsState data{};
    std::array<gps::GnssSatInfo, gps::kMaxGnssSats> sats{};
    std::size_t sat_count = 0;
    gps::GnssStatus status{};
    uint32_t last_motion_ms = 0;
    bool enabled = false;
    bool powered = false;
    uint32_t collection_interval_ms = 1000;
    uint8_t power_strategy = 0;
    uint8_t gnss_mode = 0;
    uint8_t gnss_sat_mask = 0;
    uint8_t external_nmea_output_hz = 0;
    uint8_t external_nmea_sentence_mask = 0;
    gps::GpsReceiverInitConfig receiver_init_config{};
    uint32_t motion_idle_timeout_ms = 0;
    uint8_t motion_sensor_id = 0;
    TaskHandle_t worker_handle = nullptr;
    bool stop_requested = false;
    bool probe_requested = false;
    uint32_t probe_deadline_ms = 0;
    uint32_t last_rx_ms = 0;
    uint32_t last_fix_ms = 0;
    uint32_t last_fix_log_ms = 0;
    uint32_t last_no_data_log_ms = 0;
    uint32_t last_time_sync_attempt_ms = 0;
    bool first_sentence_logged = false;
    bool time_sync_committed = false;
    std::time_t last_time_sync_epoch = 0;
    std::array<uint16_t, 12> used_sat_ids{};
    std::size_t used_sat_count = 0;
    std::array<GsvCollector, static_cast<size_t>(CollectorSlot::COUNT)> gsv{};
};

RuntimeState s_runtime{};
std::mutex s_mutex;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool is_valid_epoch(std::time_t epoch_seconds)
{
    return epoch_seconds >= kMinValidEpochSeconds;
}

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
    if (day < 1 || day > days_in_month(year, month))
    {
        return false;
    }
    if (hour > 23 || minute > 59 || second > 59)
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
    return static_cast<std::time_t>(days * 86400LL + static_cast<int64_t>(hour) * 3600LL +
                                    static_cast<int64_t>(minute) * 60LL + second);
}

struct GpsUartPins
{
    int port;
    int tx;
    int rx;
};

GpsUartPins gps_uart_pins()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    const auto& uart = ::boards::tab5::Tab5Board::gpsUart();
    return {uart.port, uart.tx, uart.rx};
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    const auto& uart = ::boards::t_display_p4::TDisplayP4Board::gpsUart();
    return {uart.port, uart.tx, uart.rx};
#else
    return {-1, -1, -1};
#endif
}

CollectorSlot collector_slot_for_talker(const char* talker)
{
    if (!talker || talker[0] == '\0' || talker[1] == '\0') return CollectorSlot::UNKNOWN;
    if (talker[0] == 'G' && talker[1] == 'P') return CollectorSlot::GPS;
    if (talker[0] == 'G' && talker[1] == 'L') return CollectorSlot::GLN;
    if (talker[0] == 'G' && talker[1] == 'A') return CollectorSlot::GAL;
    if ((talker[0] == 'G' && talker[1] == 'B') || (talker[0] == 'B' && talker[1] == 'D')) return CollectorSlot::BD;
    return CollectorSlot::UNKNOWN;
}

gps::GnssSystem system_for_slot(CollectorSlot slot)
{
    switch (slot)
    {
    case CollectorSlot::GPS:
        return gps::GnssSystem::GPS;
    case CollectorSlot::GLN:
        return gps::GnssSystem::GLN;
    case CollectorSlot::GAL:
        return gps::GnssSystem::GAL;
    case CollectorSlot::BD:
        return gps::GnssSystem::BD;
    default:
        return gps::GnssSystem::UNKNOWN;
    }
}

bool parse_uint(const char* text, uint32_t* out)
{
    if (!text || !*text || !out) return false;
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 10);
    if (end == text) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parse_int(const char* text, int* out)
{
    if (!text || !*text || !out) return false;
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text) return false;
    *out = static_cast<int>(value);
    return true;
}

bool parse_double(const char* text, double* out)
{
    if (!text || !*text || !out) return false;
    char* end = nullptr;
    double value = std::strtod(text, &end);
    if (end == text) return false;
    *out = value;
    return true;
}

bool parse_latlon(const char* value_text, const char* hemi_text, bool latitude, double* out)
{
    if (!value_text || !*value_text || !hemi_text || !*hemi_text || !out) return false;
    double raw = 0.0;
    if (!parse_double(value_text, &raw)) return false;
    int degrees = static_cast<int>(raw / 100.0);
    double minutes = raw - (static_cast<double>(degrees) * 100.0);
    double decimal = static_cast<double>(degrees) + (minutes / 60.0);
    char hemi = hemi_text[0];
    if (hemi == 'S' || hemi == 'W') decimal = -decimal;
    else if ((latitude && hemi != 'N') || (!latitude && hemi != 'E')) return false;
    *out = decimal;
    return true;
}

bool verify_checksum(const char* line)
{
    if (!line || line[0] != '$') return false;
    const char* star = std::strchr(line, '*');
    if (!star || !star[1] || !star[2]) return true;
    uint8_t checksum = 0;
    for (const char* cursor = line + 1; cursor < star; ++cursor) checksum ^= static_cast<uint8_t>(*cursor);
    char text[3] = {star[1], star[2], '\0'};
    char* end = nullptr;
    long expected = std::strtol(text, &end, 16);
    return end != text && checksum == static_cast<uint8_t>(expected & 0xFF);
}

std::size_t split_fields(char* sentence, std::array<char*, kMaxFields>& fields)
{
    fields.fill(nullptr);
    std::size_t count = 0;
    char* cursor = sentence;
    while (cursor && *cursor && count < fields.size())
    {
        fields[count++] = cursor;
        char* comma = std::strchr(cursor, ',');
        if (!comma) break;
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

void clear_payload_locked()
{
    s_runtime.data = gps::GpsState{};
    s_runtime.status = gps::GnssStatus{};
    s_runtime.sat_count = 0;
    s_runtime.used_sat_count = 0;
    s_runtime.last_time_sync_attempt_ms = 0;
    s_runtime.last_time_sync_epoch = 0;
    s_runtime.time_sync_committed = is_valid_epoch(std::time(nullptr));
    for (auto& collector : s_runtime.gsv) collector = GsvCollector{};
}

bool parse_two_digits(const char* text, uint8_t* out)
{
    if (text == nullptr || out == nullptr || text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9')
    {
        return false;
    }
    *out = static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
    return true;
}

bool parse_hhmmss(const char* text, uint8_t* hour, uint8_t* minute, uint8_t* second)
{
    if (text == nullptr || hour == nullptr || minute == nullptr || second == nullptr)
    {
        return false;
    }
    return parse_two_digits(text, hour) &&
           parse_two_digits(text + 2, minute) &&
           parse_two_digits(text + 4, second);
}

bool parse_ddmmyy(const char* text, uint8_t* day, uint8_t* month, int* year)
{
    if (text == nullptr || day == nullptr || month == nullptr || year == nullptr)
    {
        return false;
    }

    uint8_t yy = 0;
    if (!parse_two_digits(text, day) || !parse_two_digits(text + 2, month) || !parse_two_digits(text + 4, &yy))
    {
        return false;
    }

    *year = 2000 + static_cast<int>(yy);
    return true;
}

void maybe_sync_time_from_rmc_locked(const std::array<char*, kMaxFields>& fields, std::size_t count, uint32_t ts)
{
    if (count <= 9)
    {
        return;
    }

    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t day = 0;
    uint8_t month = 0;
    int year = 0;
    if (!parse_hhmmss(fields[1], &hour, &minute, &second) || !parse_ddmmyy(fields[9], &day, &month, &year))
    {
        return;
    }

    if (!validate_datetime_utc(year, month, day, hour, minute, second))
    {
        return;
    }

    const std::time_t gnss_epoch = datetime_to_epoch_utc(
        year, month, day, hour, minute, second);
    if (gnss_epoch < 0)
    {
        return;
    }

    const std::time_t now_epoch = std::time(nullptr);
    const bool system_valid = is_valid_epoch(now_epoch);
    const long long delta_seconds = system_valid
                                        ? std::llabs(static_cast<long long>(now_epoch) - static_cast<long long>(gnss_epoch))
                                        : std::numeric_limits<long long>::max();
    const bool needs_system_update = !system_valid || delta_seconds >= 2;
    const bool needs_commit = !s_runtime.time_sync_committed;
    if (!needs_system_update && !needs_commit)
    {
        return;
    }

    if (s_runtime.last_time_sync_attempt_ms != 0 && (ts - s_runtime.last_time_sync_attempt_ms) < 5000)
    {
        return;
    }

    s_runtime.last_time_sync_attempt_ms = ts;
    if (platform::esp::boards::applySystemTimeAndSyncBoardRtc(gnss_epoch, "gnss_rmc"))
    {
        s_runtime.last_time_sync_epoch = gnss_epoch;
        s_runtime.time_sync_committed = true;
    }
}

bool used_sat_locked(uint16_t sat_id)
{
    for (std::size_t i = 0; i < s_runtime.used_sat_count; ++i)
    {
        if (s_runtime.used_sat_ids[i] == sat_id) return true;
    }
    return false;
}

void merge_sats_locked()
{
    s_runtime.sat_count = 0;
    for (std::size_t ci = 0; ci < s_runtime.gsv.size(); ++ci)
    {
        auto& collector = s_runtime.gsv[ci];
        for (std::size_t si = 0; si < collector.count && s_runtime.sat_count < s_runtime.sats.size(); ++si)
        {
            auto sat = collector.sats[si];
            sat.used = used_sat_locked(sat.id);
            s_runtime.sats[s_runtime.sat_count++] = sat;
        }
    }
    s_runtime.status.sats_in_view = static_cast<uint8_t>(std::min<std::size_t>(s_runtime.sat_count, 255));
}

void parse_rmc_locked(const std::array<char*, kMaxFields>& fields, std::size_t count, uint32_t ts)
{
    if (count < 9) return;
    bool active = fields[2] && fields[2][0] == 'A';
    s_runtime.data.valid = active;
    maybe_sync_time_from_rmc_locked(fields, count, ts);
    if (!active) return;
    double lat = 0.0, lng = 0.0, speed_knots = 0.0, course = 0.0;
    const bool lat_ok = parse_latlon(fields[3], fields[4], true, &lat);
    const bool lng_ok = parse_latlon(fields[5], fields[6], false, &lng);
    if (lat_ok) s_runtime.data.lat = lat;
    if (lng_ok) s_runtime.data.lng = lng;
    if (parse_double(fields[7], &speed_knots))
    {
        s_runtime.data.speed_mps = speed_knots * 0.514444;
        s_runtime.data.has_speed = true;
    }
    else
    {
        s_runtime.data.has_speed = false;
    }
    if (parse_double(fields[8], &course))
    {
        s_runtime.data.course_deg = course;
        s_runtime.data.has_course = true;
    }
    else
    {
        s_runtime.data.has_course = false;
    }
    s_runtime.last_fix_ms = ts;
    if (lat_ok && lng_ok &&
        (s_runtime.last_fix_log_ms == 0 || (ts - s_runtime.last_fix_log_ms) >= 5000U))
    {
        ESP_LOGI(kTag,
                 "GNSS fix lat=%.6f lng=%.6f sats=%u age_ms=0",
                 s_runtime.data.lat,
                 s_runtime.data.lng,
                 static_cast<unsigned>(s_runtime.data.satellites));
        s_runtime.last_fix_log_ms = ts;
    }
}

void parse_gga_locked(const std::array<char*, kMaxFields>& fields, std::size_t count, uint32_t ts)
{
    if (count < 10) return;
    int quality = 0;
    if (parse_int(fields[6], &quality)) s_runtime.data.valid = quality > 0;
    uint32_t satellites = 0;
    if (parse_uint(fields[7], &satellites))
    {
        s_runtime.data.satellites = static_cast<uint8_t>(std::min<uint32_t>(satellites, 255));
        s_runtime.status.sats_in_use = s_runtime.data.satellites;
    }
    double hdop = 0.0, altitude = 0.0;
    if (parse_double(fields[8], &hdop)) s_runtime.status.hdop = static_cast<float>(hdop);
    if (parse_double(fields[9], &altitude))
    {
        s_runtime.data.alt_m = altitude;
        s_runtime.data.has_alt = true;
    }
    else
    {
        s_runtime.data.has_alt = false;
    }
    if (s_runtime.data.valid) s_runtime.last_fix_ms = ts;
}

void parse_gsa_locked(const std::array<char*, kMaxFields>& fields, std::size_t count)
{
    if (count < 4) return;
    s_runtime.used_sat_count = 0;
    for (std::size_t i = 3; i <= 14 && i < count; ++i)
    {
        uint32_t sat_id = 0;
        if (parse_uint(fields[i], &sat_id) && sat_id > 0 && s_runtime.used_sat_count < s_runtime.used_sat_ids.size())
            s_runtime.used_sat_ids[s_runtime.used_sat_count++] = static_cast<uint16_t>(sat_id);
    }
    int fix_type = 0;
    if (parse_int(fields[2], &fix_type))
    {
        s_runtime.status.fix = fix_type >= 3 ? gps::GnssFix::FIX3D : (fix_type == 2 ? gps::GnssFix::FIX2D : gps::GnssFix::NOFIX);
    }
    if (count > 16)
    {
        double hdop = 0.0;
        if (parse_double(fields[16], &hdop)) s_runtime.status.hdop = static_cast<float>(hdop);
    }
    s_runtime.status.sats_in_use = static_cast<uint8_t>(std::min<std::size_t>(s_runtime.used_sat_count, 255));
    merge_sats_locked();
}

void parse_gsv_locked(const char* talker, const std::array<char*, kMaxFields>& fields, std::size_t count)
{
    if (count < 4) return;
    auto& collector = s_runtime.gsv[static_cast<std::size_t>(collector_slot_for_talker(talker))];
    uint32_t msg_num = 0;
    if (!parse_uint(fields[2], &msg_num)) return;
    if (msg_num == 1) collector.count = 0;
    for (std::size_t base = 4; base + 3 < count && collector.count < collector.sats.size(); base += 4)
    {
        uint32_t sat_id = 0;
        if (!parse_uint(fields[base], &sat_id) || sat_id == 0) continue;
        gps::GnssSatInfo sat{};
        sat.id = static_cast<uint16_t>(sat_id);
        sat.sys = system_for_slot(collector_slot_for_talker(talker));
        uint32_t elev = 0, az = 0;
        int snr = -1;
        if (parse_uint(fields[base + 1], &elev)) sat.elevation = static_cast<uint8_t>(std::min<uint32_t>(elev, 90));
        if (parse_uint(fields[base + 2], &az)) sat.azimuth = static_cast<uint16_t>(std::min<uint32_t>(az, 359));
        if (parse_int(fields[base + 3], &snr)) sat.snr = static_cast<int8_t>(std::clamp(snr, -1, 99));
        collector.sats[collector.count++] = sat;
    }
    merge_sats_locked();
}

NmeaParseResult parse_sentence_locked(const char* sentence, uint32_t ts)
{
    if (!sentence || sentence[0] != '$') return NmeaParseResult::MissingDollar;
    if (!verify_checksum(sentence)) return NmeaParseResult::InvalidChecksum;
    char working[kLineBufferSize] = {};
    std::strncpy(working, sentence + 1, sizeof(working) - 1);
    char* star = std::strchr(working, '*');
    if (star) *star = '\0';
    std::array<char*, kMaxFields> fields{};
    std::size_t count = split_fields(working, fields);
    if (count == 0 || !fields[0] || std::strlen(fields[0]) < 5)
    {
        return NmeaParseResult::Malformed;
    }
    char talker[3] = {fields[0][0], fields[0][1], '\0'};
    const char* type = fields[0] + std::strlen(fields[0]) - 3;
    s_runtime.last_rx_ms = ts;
    if (!s_runtime.first_sentence_logged)
    {
        ESP_LOGI(kTag, "GNSS first NMEA sentence: %s", sentence);
        s_runtime.first_sentence_logged = true;
    }
    if (std::strcmp(type, "RMC") == 0) parse_rmc_locked(fields, count, ts);
    else if (std::strcmp(type, "GGA") == 0) parse_gga_locked(fields, count, ts);
    else if (std::strcmp(type, "GSA") == 0) parse_gsa_locked(fields, count);
    else if (std::strcmp(type, "GSV") == 0) parse_gsv_locked(talker, fields, count);
    else return NmeaParseResult::Unsupported;
    return NmeaParseResult::Accepted;
}

void record_nmea_parse_result(GpsRxDiagnostics* diagnostics, NmeaParseResult result)
{
    if (!diagnostics) return;
    switch (result)
    {
    case NmeaParseResult::Accepted:
        ++diagnostics->accepted_sentences;
        break;
    case NmeaParseResult::Unsupported:
        ++diagnostics->unsupported_sentences;
        break;
    case NmeaParseResult::InvalidChecksum:
        ++diagnostics->invalid_checksums;
        break;
    case NmeaParseResult::Malformed:
    case NmeaParseResult::MissingDollar:
        ++diagnostics->malformed_sentences;
        break;
    }
}

void save_raw_diagnostic_sample(GpsRxDiagnostics* diagnostics, const uint8_t* buffer, int length)
{
    if (!diagnostics || !buffer || length <= 0) return;
    diagnostics->raw_sample_len =
        std::min<std::size_t>(static_cast<std::size_t>(length), diagnostics->raw_sample.size());
    std::memcpy(diagnostics->raw_sample.data(), buffer, diagnostics->raw_sample_len);
}

void format_raw_diagnostic_sample(const GpsRxDiagnostics& diagnostics,
                                  char* output,
                                  std::size_t output_size)
{
    if (!output || output_size == 0) return;
    output[0] = '\0';
    std::size_t used = 0;
    for (std::size_t i = 0; i < diagnostics.raw_sample_len && used + 3 < output_size; ++i)
    {
        const int written = std::snprintf(output + used,
                                          output_size - used,
                                          "%02X%s",
                                          static_cast<unsigned>(diagnostics.raw_sample[i]),
                                          i + 1 == diagnostics.raw_sample_len ? "" : " ");
        if (written <= 0) break;
        used += static_cast<std::size_t>(written);
    }
}

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
bool should_try_p4_factory_baud(bool attempted, uint8_t configured_baud,
                                bool valid_nmea_seen, uint32_t elapsed_ms)
{
    return !attempted && configured_baud == 0 && !valid_nmea_seen && elapsed_ms >= kNoDataWarnMs;
}
#endif

void append_bytes_and_process(const uint8_t* buffer,
                              int length,
                              char* line_buffer,
                              std::size_t* line_length,
                              GpsRxDiagnostics* diagnostics)
{
    if (!buffer || length <= 0 || !line_buffer || !line_length) return;
    save_raw_diagnostic_sample(diagnostics, buffer, length);
    for (int i = 0; i < length; ++i)
    {
        char ch = static_cast<char>(buffer[i]);
        if (ch == '\r' || ch == '\n')
        {
            if (*line_length > 0)
            {
                line_buffer[*line_length] = '\0';
                std::lock_guard<std::mutex> lock(s_mutex);
                const NmeaParseResult result = parse_sentence_locked(line_buffer, now_ms());
                if (diagnostics)
                {
                    ++diagnostics->completed_lines;
                    record_nmea_parse_result(diagnostics, result);
                }
                *line_length = 0;
            }
            continue;
        }
        if (*line_length == 0 && ch != '$')
        {
            if (diagnostics) ++diagnostics->discarded_prefix_bytes;
            continue;
        }
        if (*line_length + 1 < kLineBufferSize) line_buffer[(*line_length)++] = ch;
        else
        {
            if (diagnostics) ++diagnostics->line_overflows;
            *line_length = 0;
        }
    }
}

bool configure_uart_hardware()
{
    gps::GpsReceiverInitConfig receiver_config{};
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        receiver_config = s_runtime.receiver_init_config;
    }
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (!::boards::tab5::Tab5Board::instance().configureGpsUart())
    {
        ESP_LOGE(kTag, "Tab5 GNSS UART setup failed");
        return false;
    }
    return true;
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    if (!::boards::t_display_p4::TDisplayP4Board::instance().prepareGpsRuntime(receiver_config.baud))
    {
        ESP_LOGE(kTag, "T-Display-P4 GNSS runtime setup failed");
        return false;
    }
    return true;
#else
    const auto gps_uart = gps_uart_pins();
    uart_config_t config{};
    config.baud_rate = receiver_config.baud != 0 ? static_cast<int>(receiver_config.baud) : 38400;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    uart_driver_delete(static_cast<uart_port_t>(gps_uart.port));
    ESP_ERROR_CHECK(uart_param_config(static_cast<uart_port_t>(gps_uart.port), &config));
    ESP_ERROR_CHECK(uart_set_pin(static_cast<uart_port_t>(gps_uart.port), gps_uart.tx, gps_uart.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(static_cast<uart_port_t>(gps_uart.port), 4096, 0, 0, nullptr, 0));
    ESP_LOGI(kTag, "GNSS UART configured: port=%d tx=%d rx=%d baud=%d", gps_uart.port, gps_uart.tx, gps_uart.rx, config.baud_rate);
    return true;
#endif
}

void teardown_uart_hardware()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    // Tab5 keeps GNSS power and UART under board ownership for the whole runtime.
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    ::boards::t_display_p4::TDisplayP4Board::instance().teardownGpsRuntime();
#else
    const auto gps_uart = gps_uart_pins();
    if (gps_uart.port >= 0)
    {
        uart_driver_delete(static_cast<uart_port_t>(gps_uart.port));
    }
#endif
}

void worker_task(void*)
{
    vTaskDelay(pdMS_TO_TICKS(kProbeWarmupMs));
    if (!configure_uart_hardware())
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_runtime.powered = false;
        s_runtime.stop_requested = false;
        s_runtime.probe_requested = false;
        s_runtime.probe_deadline_ms = 0;
        s_runtime.worker_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_runtime.powered = true;
        s_runtime.last_rx_ms = 0;
        s_runtime.last_fix_ms = 0;
        s_runtime.first_sentence_logged = false;
        s_runtime.last_no_data_log_ms = 0;
    }

    char line_buffer[kLineBufferSize] = {};
    std::size_t line_length = 0;
    uint8_t rx_buffer[kRxBufferSize] = {};
    uint64_t raw_rx_bytes = 0;
    uint32_t last_raw_rx_ms = 0;
    GpsRxDiagnostics rx_diagnostics{};
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    bool p4_auto_baud_fallback_attempted = false;
    const uint32_t p4_uart_started_ms = now_ms();
#endif

    while (true)
    {
        const auto gps_uart = gps_uart_pins();
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
        const auto port = ::boards::tab5::Tab5Board::instance().gpsUartPort();
#else
        const auto port = static_cast<uart_port_t>(gps_uart.port);
#endif
        int read_len = uart_read_bytes(port, rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(200));
        if (read_len > 0)
        {
            raw_rx_bytes += static_cast<uint64_t>(read_len);
            last_raw_rx_ms = now_ms();
            append_bytes_and_process(rx_buffer,
                                     read_len,
                                     line_buffer,
                                     &line_length,
                                     &rx_diagnostics);
        }

        bool should_exit = false;
        bool enabled = false;
        bool probe_requested = false;
        uint32_t probe_deadline_ms = 0;
        uint32_t last_rx_ms = 0;
        uint32_t last_fix_ms = 0;
        uint32_t now = now_ms();
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        bool switch_to_p4_fallback_baud = false;
#endif
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            enabled = s_runtime.enabled;
            probe_requested = s_runtime.probe_requested;
            probe_deadline_ms = s_runtime.probe_deadline_ms;
            last_rx_ms = s_runtime.last_rx_ms;
            last_fix_ms = s_runtime.last_fix_ms;
            s_runtime.data.age = last_fix_ms ? (now - last_fix_ms) : 0;
            if (s_runtime.stop_requested && !enabled) should_exit = true;
            else if (!enabled && probe_requested && probe_deadline_ms != 0 && now >= probe_deadline_ms)
            {
                ESP_LOGI(kTag, "GNSS startup probe finished: nmea_seen=%d last_fix_age_ms=%lu", last_rx_ms != 0 ? 1 : 0, last_fix_ms != 0 ? static_cast<unsigned long>(now - last_fix_ms) : 0UL);
                s_runtime.probe_requested = false;
                should_exit = true;
            }
            else if ((now - last_rx_ms) >= kNoDataWarnMs && s_runtime.last_no_data_log_ms + kNoDataWarnMs <= now)
            {
                const auto gps_uart_log = gps_uart_pins();
                char raw_sample[3 * kRawDiagnosticSampleBytes] = {};
                format_raw_diagnostic_sample(rx_diagnostics, raw_sample, sizeof(raw_sample));
                ESP_LOGW(kTag,
                         "GNSS parser has no valid NMEA: port=%d tx=%d rx=%d mode=%u raw_bytes=%llu last_raw_ms=%u lines=%lu valid=%lu unsupported=%lu bad_checksum=%lu malformed=%lu prefix_bytes=%llu overflow=%lu partial_len=%u raw_hex=%s",
                         gps_uart_log.port,
                         gps_uart_log.tx,
                         gps_uart_log.rx,
                         s_runtime.gnss_mode,
                         static_cast<unsigned long long>(raw_rx_bytes),
                         last_raw_rx_ms,
                         static_cast<unsigned long>(rx_diagnostics.completed_lines),
                         static_cast<unsigned long>(rx_diagnostics.accepted_sentences),
                         static_cast<unsigned long>(rx_diagnostics.unsupported_sentences),
                         static_cast<unsigned long>(rx_diagnostics.invalid_checksums),
                         static_cast<unsigned long>(rx_diagnostics.malformed_sentences),
                         static_cast<unsigned long long>(rx_diagnostics.discarded_prefix_bytes),
                         static_cast<unsigned long>(rx_diagnostics.line_overflows),
                         static_cast<unsigned>(line_length),
                         raw_sample[0] != '\0' ? raw_sample : "none");
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
                // Factory L76K firmware can still use 9600. A baud mismatch
                // can produce silence, zeroes, or arbitrary framing errors.
                // Only try the factory rate before ANY valid NMEA was seen;
                // losing reception later must not change a working baud.
                if (should_try_p4_factory_baud(p4_auto_baud_fallback_attempted,
                                               s_runtime.receiver_init_config.baud,
                                               s_runtime.first_sentence_logged,
                                               now - p4_uart_started_ms))
                {
                    p4_auto_baud_fallback_attempted = true;
                    switch_to_p4_fallback_baud = true;
                }
#endif
                s_runtime.last_no_data_log_ms = now;
            }
        }
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        if (switch_to_p4_fallback_baud)
        {
            ESP_LOGW(kTag,
                     "P4 GNSS auto-baud fallback: no valid NMEA with Settings baud=Auto; cycling wake then trying %lu",
                     static_cast<unsigned long>(kP4GnssFallbackBaud));
            teardown_uart_hardware();
            vTaskDelay(pdMS_TO_TICKS(kP4GnssFallbackWakeCycleMs));
            if (::boards::t_display_p4::TDisplayP4Board::instance().prepareGpsRuntime(
                    kP4GnssFallbackBaud))
            {
                ESP_LOGI(kTag,
                         "P4 GNSS auto-baud fallback active: port=%d tx=%d rx=%d baud=%lu",
                         gps_uart.port,
                         gps_uart.tx,
                         gps_uart.rx,
                         static_cast<unsigned long>(kP4GnssFallbackBaud));
                std::memset(line_buffer, 0, sizeof(line_buffer));
                line_length = 0;
                raw_rx_bytes = 0;
                last_raw_rx_ms = 0;
                rx_diagnostics = GpsRxDiagnostics{};
            }
            else
            {
                ESP_LOGE(kTag, "P4 GNSS auto-baud fallback UART setup failed");
                (void)::boards::t_display_p4::TDisplayP4Board::instance().prepareGpsRuntime();
            }
            continue;
        }
#endif
        if (should_exit) break;
    }

    teardown_uart_hardware();
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_runtime.enabled) clear_payload_locked();
        s_runtime.powered = false;
        s_runtime.stop_requested = false;
        s_runtime.worker_handle = nullptr;
        s_runtime.last_rx_ms = 0;
        s_runtime.last_fix_ms = 0;
        s_runtime.last_fix_log_ms = 0;
        s_runtime.last_time_sync_attempt_ms = 0;
        s_runtime.last_time_sync_epoch = 0;
        s_runtime.time_sync_committed = is_valid_epoch(std::time(nullptr));
        s_runtime.first_sentence_logged = false;
        s_runtime.last_no_data_log_ms = 0;
    }
    vTaskDelete(nullptr);
}

void ensure_worker_locked()
{
    if (s_runtime.worker_handle != nullptr) return;
    s_runtime.stop_requested = false;
    if (xTaskCreate(worker_task, "idf_gps", 6144, nullptr, 5, &s_runtime.worker_handle) != pdPASS)
    {
        ESP_LOGE(kTag, "Failed to create GNSS runtime task");
        s_runtime.worker_handle = nullptr;
    }
}

} // namespace

gps::GpsState get_data()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    gps::GpsState data = s_runtime.data;
    data.age = s_runtime.last_fix_ms ? (now_ms() - s_runtime.last_fix_ms) : 0;
    return data;
}

bool get_gnss_snapshot(gps::GnssSatInfo* out, std::size_t max, std::size_t* out_count, gps::GnssStatus* status)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (out_count) *out_count = s_runtime.sat_count;
    if (status) *status = s_runtime.status;
    if (!out || max == 0 || s_runtime.sat_count == 0) return false;
    std::size_t count = std::min(max, s_runtime.sat_count);
    for (std::size_t i = 0; i < count; ++i) out[i] = s_runtime.sats[i];
    if (out_count) *out_count = count;
    return true;
}

gps::GpsDiagnosticsSnapshot diagnostics()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    gps::GpsDiagnosticsSnapshot snapshot{};
    snapshot.supported = platform::ui::device::gps_supported();
    snapshot.enabled = snapshot.supported && s_runtime.enabled;
    snapshot.powered = snapshot.supported && s_runtime.powered;
    snapshot.ready = s_runtime.worker_handle != nullptr;
    snapshot.has_fix = s_runtime.data.valid;
    snapshot.satellites = s_runtime.data.satellites;
    snapshot.sats_in_view = s_runtime.status.sats_in_view;
    snapshot.sats_in_use = s_runtime.status.sats_in_use;
    snapshot.last_rx_age_ms = s_runtime.last_rx_ms ? (now_ms() - s_runtime.last_rx_ms) : 0xFFFFFFFFUL;
    snapshot.poll_interval_ms = 200;
    snapshot.collection_interval_ms = s_runtime.collection_interval_ms;

    if (!snapshot.supported)
    {
        snapshot.code = gps::GpsDiagnosticCode::Disabled;
    }
    else if (!snapshot.enabled)
    {
        snapshot.code = gps::GpsDiagnosticCode::NotEnabled;
    }
    else if (!snapshot.powered)
    {
        snapshot.code = gps::GpsDiagnosticCode::PowerOff;
    }
    else if (!snapshot.ready)
    {
        snapshot.code = gps::GpsDiagnosticCode::TransportNotReady;
    }
    else if (snapshot.last_rx_age_ms == 0xFFFFFFFFUL)
    {
        snapshot.code = gps::GpsDiagnosticCode::NoTraffic;
    }
    else if (snapshot.last_rx_age_ms > kNoDataWarnMs)
    {
        snapshot.code = gps::GpsDiagnosticCode::TrafficStalled;
    }
    else if (!snapshot.has_fix)
    {
        snapshot.code = gps::GpsDiagnosticCode::NoFix;
    }
    else
    {
        snapshot.code = gps::GpsDiagnosticCode::OK;
    }
    return snapshot;
}

uint32_t last_motion_ms()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_runtime.last_motion_ms;
}

bool is_enabled()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return platform::ui::device::gps_supported() && s_runtime.enabled;
}

bool is_powered()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return platform::ui::device::gps_supported() && s_runtime.powered;
}

void set_collection_interval(uint32_t interval_ms)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.collection_interval_ms = interval_ms;
}

void set_power_strategy(uint8_t strategy)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.power_strategy = strategy;
}

void set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.enabled = platform::ui::device::gps_supported() && enabled;
    if (!s_runtime.enabled)
    {
        s_runtime.stop_requested = true;
        clear_payload_locked();
        ESP_LOGI(kTag, "GNSS runtime disabled");
        return;
    }
    s_runtime.probe_requested = false;
    s_runtime.probe_deadline_ms = 0;
    s_runtime.time_sync_committed = is_valid_epoch(std::time(nullptr));
    ensure_worker_locked();
    ESP_LOGI(kTag,
             "GNSS runtime enabled: interval_ms=%lu mode=%u sat_mask=0x%02X strategy=%u",
             static_cast<unsigned long>(s_runtime.collection_interval_ms),
             s_runtime.gnss_mode,
             s_runtime.gnss_sat_mask,
             s_runtime.power_strategy);
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.gnss_mode = mode;
    s_runtime.gnss_sat_mask = sat_mask;
    if (s_runtime.enabled)
    {
        ensure_worker_locked();
    }
    ESP_LOGI(kTag, "GNSS receiver config: mode=%u sat_mask=0x%02X enabled=%d",
             s_runtime.gnss_mode,
             s_runtime.gnss_sat_mask,
             s_runtime.enabled ? 1 : 0);
}

void set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.external_nmea_output_hz = output_hz;
    s_runtime.external_nmea_sentence_mask = sentence_mask;
}

void set_receiver_init_config(const gps::GpsReceiverInitConfig& config)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    const bool baud_changed = s_runtime.receiver_init_config.baud != config.baud;
    s_runtime.receiver_init_config = config;
    ESP_LOGI(kTag,
             "GNSS receiver init config applied: baud=%lu probe_ms=%lu profile=%u policies=%u/%u/%u%s",
             static_cast<unsigned long>(config.baud),
             static_cast<unsigned long>(config.probe_ms),
             config.profile,
             config.rxm_policy,
             config.gnss_policy,
             config.nmea_policy,
             baud_changed && s_runtime.worker_handle != nullptr
                 ? " (new baud takes effect on next GNSS lifecycle)"
                 : "");
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.motion_idle_timeout_ms = timeout_ms;
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_runtime.motion_sensor_id = sensor_id;
}

TaskHandle_t get_task_handle()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_runtime.worker_handle;
}

void request_startup_probe()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!platform::ui::device::gps_supported() || s_runtime.enabled || s_runtime.worker_handle != nullptr) return;
    s_runtime.probe_requested = true;
    s_runtime.probe_deadline_ms = now_ms() + kProbeWarmupMs + kProbeListenMs;
    ensure_worker_locked();
    const auto gps_uart = gps_uart_pins();
    ESP_LOGI(kTag, "GNSS startup probe scheduled: port=%d tx=%d rx=%d mode=%u sat_mask=0x%02X", gps_uart.port, gps_uart.tx, gps_uart.rx, s_runtime.gnss_mode, s_runtime.gnss_sat_mask);
}

} // namespace platform::esp::idf_common::gps_runtime
