#include "boards/t_display_p4/haptic_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)

#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace boards::t_display_p4::haptic_runtime
{
namespace
{

constexpr const char* kTag = "p4-haptic";
constexpr uint8_t kRegSoftwareReset = 0x00;
constexpr uint8_t kRegPlayConfig3 = 0x08;
constexpr uint8_t kRegPlayConfig4 = 0x09;
constexpr uint8_t kRegRtpData = 0x32;
constexpr uint8_t kRegGlobalState = 0x3F;
constexpr uint8_t kRegSystemControl2 = 0x43;
constexpr uint8_t kRegDeviceId = 0x64;
constexpr uint8_t kSoftwareResetValue = 0xAA;
constexpr uint8_t kRtpPlayMode = 0x01;
constexpr uint8_t kRtpGlobalState = 0x08;
constexpr uint8_t kSampleRate12Khz = 0x02;
constexpr uint32_t kI2cTimeoutMs = 60;
constexpr std::size_t kWaveformSampleCount = 240;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kWaveformFrequencyHz = 170.0f;
constexpr float kWaveformSampleRateHz = 12000.0f;

struct Runtime
{
    std::mutex mutex;
    i2c_master_dev_handle_t device = nullptr;
    bool ready = false;
    std::array<uint8_t, kWaveformSampleCount + 1> rtp_payload{};
};

Runtime g_runtime{};

bool read_register(uint8_t reg, uint8_t* out_value)
{
    return g_runtime.device && out_value &&
           i2c_master_transmit_receive(g_runtime.device,
                                       &reg,
                                       1,
                                       out_value,
                                       1,
                                       kI2cTimeoutMs) == ESP_OK;
}

bool write_register(uint8_t reg, uint8_t value)
{
    const uint8_t payload[] = {reg, value};
    return g_runtime.device &&
           i2c_master_transmit(g_runtime.device,
                               payload,
                               sizeof(payload),
                               kI2cTimeoutMs) == ESP_OK;
}

bool update_register(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    if (!read_register(reg, &current))
    {
        return false;
    }
    return write_register(reg,
                          static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) |
                                               (value & mask)));
}

void build_click_waveform()
{
    g_runtime.rtp_payload[0] = kRegRtpData;
    for (std::size_t index = 0; index < kWaveformSampleCount; ++index)
    {
        const float phase = 2.0f * kPi * kWaveformFrequencyHz *
                            static_cast<float>(index) / kWaveformSampleRateHz;
        const float normalized_index = static_cast<float>(index) /
                                       static_cast<float>(kWaveformSampleCount - 1);
        const float envelope = std::sin(kPi * normalized_index);
        const int8_t sample = static_cast<int8_t>(std::sin(phase) * envelope * 96.0f);
        g_runtime.rtp_payload[index + 1] = static_cast<uint8_t>(sample);
    }
}

bool initialize_locked()
{
    if (g_runtime.ready)
    {
        return true;
    }

    auto& board = TDisplayP4Board::instance();
    if (!board.profile().has_haptic || !board.ensureExternal3v3Power())
    {
        return false;
    }
    const i2c_master_bus_handle_t bus = board.externalI2cHandle();
    if (!bus)
    {
        return false;
    }
    if (i2c_master_probe(bus, board.profile().i2c.haptic, kI2cTimeoutMs) != ESP_OK)
    {
        return false;
    }
    if (!g_runtime.device)
    {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = board.profile().i2c.haptic;
        config.scl_speed_hz = 400000;
        const esp_err_t err = i2c_master_bus_add_device(bus, &config, &g_runtime.device);
        if (err != ESP_OK)
        {
            ESP_LOGE(kTag, "AW86224 add device failed: %s", esp_err_to_name(err));
            g_runtime.device = nullptr;
            return false;
        }
    }

    uint8_t device_id = 0;
    if (!read_register(kRegDeviceId, &device_id))
    {
        return false;
    }
    if (!write_register(kRegSoftwareReset, kSoftwareResetValue))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(3));
    if (!update_register(kRegSystemControl2, 0x03, kSampleRate12Khz))
    {
        return false;
    }
    build_click_waveform();
    g_runtime.ready = true;
    ESP_LOGI(kTag, "AW86224 ready id=0x%02X addr=0x%02X",
             device_id,
             static_cast<unsigned>(board.profile().i2c.haptic));
    return true;
}

bool stop_locked()
{
    return !g_runtime.device || update_register(kRegPlayConfig4, 0x02, 0x02);
}

} // namespace

bool trigger()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!initialize_locked() || !stop_locked() ||
        !update_register(kRegPlayConfig3, 0x03, kRtpPlayMode) ||
        !update_register(kRegPlayConfig4, 0x01, 0x01))
    {
        g_runtime.ready = false;
        return false;
    }

    for (unsigned attempt = 0; attempt < 20; ++attempt)
    {
        uint8_t state = 0;
        if (read_register(kRegGlobalState, &state) &&
            (state & 0x0F) == kRtpGlobalState)
        {
            const bool written = i2c_master_transmit(g_runtime.device,
                                                     g_runtime.rtp_payload.data(),
                                                     g_runtime.rtp_payload.size(),
                                                     kI2cTimeoutMs) == ESP_OK;
            if (!written) g_runtime.ready = false;
            return written;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(kTag, "AW86224 failed to enter RTP mode");
    g_runtime.ready = false;
    return false;
}

void stop()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!stop_locked()) g_runtime.ready = false;
}

bool is_ready()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    return initialize_locked();
}

} // namespace boards::t_display_p4::haptic_runtime

#else

namespace boards::t_display_p4::haptic_runtime
{

bool trigger()
{
    return false;
}

void stop() {}

bool is_ready()
{
    return false;
}

} // namespace boards::t_display_p4::haptic_runtime

#endif
