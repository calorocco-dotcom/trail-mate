#include "boards/t_display_p4/heading_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace boards::t_display_p4::heading_runtime
{
namespace
{

constexpr const char* kTag = "p4-heading";
constexpr uint16_t kIcm20948Address = 0x68;
constexpr uint16_t kAk09916Address = 0x0C;
constexpr uint8_t kRegBankSelect = 0x7F;
constexpr uint8_t kWhoAmI = 0x00;
constexpr uint8_t kUserControl = 0x03;
constexpr uint8_t kPowerManagement1 = 0x06;
constexpr uint8_t kInterruptPinConfig = 0x0F;
constexpr uint8_t kAkWhoAmI1 = 0x00;
constexpr uint8_t kAkWhoAmI2 = 0x01;
constexpr uint8_t kAkStatus1 = 0x10;
constexpr uint8_t kAkControl2 = 0x31;
constexpr uint8_t kAkControl3 = 0x32;
constexpr uint8_t kIcmWhoAmIValue = 0xEA;
constexpr uint8_t kAkWhoAmI1Value = 0x48;
constexpr uint8_t kAkWhoAmI2Value = 0x09;
constexpr uint8_t kIcmReset = 0x80;
constexpr uint8_t kIcmClockAuto = 0x01;
constexpr uint8_t kI2cBypassEnable = 0x02;
constexpr uint8_t kAkContinuous20Hz = 0x04;
constexpr uint32_t kI2cTimeoutMs = 60;
constexpr uint32_t kSampleIntervalMs = 100;
constexpr uint32_t kStaleAfterMs = 1500;
constexpr uint32_t kLogIntervalMs = 3000;
constexpr float kPi = 3.14159265358979323846f;

struct RuntimeState
{
    std::mutex mutex;
    HeadingState data{};
    bool started = false;
    bool pinned = false;
    uint32_t consumers = 0;
    bool sensor_ready = false;
    uint32_t retry_backoff_ms = 1000;
    uint32_t next_retry_ms = 0;
    uint32_t last_log_ms = 0;
    TaskHandle_t task = nullptr;
    i2c_master_dev_handle_t icm = nullptr;
    i2c_master_dev_handle_t magnetometer = nullptr;
};

RuntimeState g_runtime{};

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool add_device(i2c_master_bus_handle_t bus,
                uint16_t address,
                i2c_master_dev_handle_t* out_handle)
{
    if (!bus || !out_handle)
    {
        return false;
    }
    if (*out_handle)
    {
        return true;
    }

    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 400000;
    const esp_err_t err = i2c_master_bus_add_device(bus, &config, out_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "add I2C device 0x%02X failed: %s", address, esp_err_to_name(err));
        *out_handle = nullptr;
        return false;
    }
    return true;
}

bool write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return device &&
           i2c_master_transmit(device, bytes, sizeof(bytes), kI2cTimeoutMs) == ESP_OK;
}

bool read_registers(i2c_master_dev_handle_t device,
                    uint8_t reg,
                    uint8_t* data,
                    size_t size)
{
    return device && data && size > 0 &&
           i2c_master_transmit_receive(device, &reg, 1, data, size, kI2cTimeoutMs) == ESP_OK;
}

bool select_bank(uint8_t bank)
{
    return write_register(g_runtime.icm, kRegBankSelect, static_cast<uint8_t>(bank << 4U));
}

bool initialize_sensor()
{
    auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
    if (!board.ensureExternal3v3Power())
    {
        return false;
    }
    const i2c_master_bus_handle_t bus = board.externalI2cHandle();
    if (!add_device(bus, kIcm20948Address, &g_runtime.icm) ||
        !add_device(bus, kAk09916Address, &g_runtime.magnetometer))
    {
        return false;
    }

    if (!select_bank(0))
    {
        return false;
    }
    uint8_t who_am_i = 0;
    if (!read_registers(g_runtime.icm, kWhoAmI, &who_am_i, 1) ||
        who_am_i != kIcmWhoAmIValue)
    {
        ESP_LOGW(kTag, "ICM20948 probe failed who_am_i=0x%02X", who_am_i);
        return false;
    }

    if (!write_register(g_runtime.icm, kPowerManagement1, kIcmReset))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!select_bank(0) ||
        !write_register(g_runtime.icm, kPowerManagement1, kIcmClockAuto) ||
        !write_register(g_runtime.icm, kUserControl, 0x00) ||
        !write_register(g_runtime.icm, kInterruptPinConfig, kI2cBypassEnable))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t magnetometer_id[2] = {};
    if (!read_registers(g_runtime.magnetometer,
                        kAkWhoAmI1,
                        magnetometer_id,
                        sizeof(magnetometer_id)) ||
        magnetometer_id[0] != kAkWhoAmI1Value ||
        magnetometer_id[1] != kAkWhoAmI2Value)
    {
        ESP_LOGW(kTag,
                 "AK09916 probe failed who_am_i=%02X:%02X",
                 magnetometer_id[0],
                 magnetometer_id[1]);
        return false;
    }
    if (!write_register(g_runtime.magnetometer, kAkControl3, 0x01))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!write_register(g_runtime.magnetometer, kAkControl2, kAkContinuous20Hz))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.sensor_ready = true;
        g_runtime.data.sensor_ready = true;
        g_runtime.data.last_update_ms = now_ms();
    }
    ESP_LOGI(kTag, "ICM20948 + AK09916 ready on external I2C");
    return true;
}

bool ensure_sensor_ready()
{
    if (g_runtime.sensor_ready)
    {
        return true;
    }
    const uint32_t now = now_ms();
    if (g_runtime.next_retry_ms != 0 && static_cast<int32_t>(now - g_runtime.next_retry_ms) < 0)
    {
        return false;
    }
    if (initialize_sensor())
    {
        g_runtime.retry_backoff_ms = 1000;
        g_runtime.next_retry_ms = 0;
        return true;
    }
    g_runtime.next_retry_ms = now_ms() + g_runtime.retry_backoff_ms;
    g_runtime.retry_backoff_ms = std::min<uint32_t>(g_runtime.retry_backoff_ms * 2U, 15000U);
    return false;
}

bool sample_once()
{
    // ST1, X/Y/Z, temperature and ST2 occupy 0x10..0x18. Reading ST2
    // terminates the AK09916 data read and releases its measurement latch.
    uint8_t sample[9] = {};
    if (!read_registers(g_runtime.magnetometer, kAkStatus1, sample, sizeof(sample)))
    {
        return false;
    }
    if ((sample[0] & 0x01U) == 0 || (sample[8] & 0x08U) != 0)
    {
        return false;
    }

    const int16_t raw_x = static_cast<int16_t>(
        static_cast<uint16_t>(sample[1]) | (static_cast<uint16_t>(sample[2]) << 8U));
    const int16_t raw_y = static_cast<int16_t>(
        static_cast<uint16_t>(sample[3]) | (static_cast<uint16_t>(sample[4]) << 8U));
    const int16_t raw_z = static_cast<int16_t>(
        static_cast<uint16_t>(sample[5]) | (static_cast<uint16_t>(sample[6]) << 8U));
    if (raw_x == 0 && raw_y == 0)
    {
        return false;
    }

    float heading = std::atan2(static_cast<float>(raw_y), static_cast<float>(raw_x)) *
                    (180.0f / kPi);
    if (heading < 0.0f)
    {
        heading += 360.0f;
    }

    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.data.available = true;
    g_runtime.data.sensor_ready = true;
    g_runtime.data.heading_deg = heading;
    g_runtime.data.raw_x = raw_x;
    g_runtime.data.raw_y = raw_y;
    g_runtime.data.raw_z = raw_z;
    g_runtime.data.last_update_ms = now_ms();
    return true;
}

void mark_stale_if_needed()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.data.sensor_ready = g_runtime.sensor_ready;
    if (now_ms() - g_runtime.data.last_update_ms > kStaleAfterMs)
    {
        g_runtime.data.available = false;
        // An unplugged or reset sensor must be probed again. Otherwise the
        // cached readiness bit prevents recovery for the rest of the session.
        g_runtime.sensor_ready = false;
        g_runtime.data.sensor_ready = false;
    }
}

void stop_sensor()
{
    // These device handles belong exclusively to the heading driver. Leave
    // the external 3V3 rail available to GNSS, radio, keyboard and expansion.
    if (g_runtime.magnetometer)
    {
        (void)write_register(g_runtime.magnetometer, kAkControl2, 0);
        (void)i2c_master_bus_rm_device(g_runtime.magnetometer);
        g_runtime.magnetometer = nullptr;
    }
    if (g_runtime.icm)
    {
        (void)select_bank(0);
        (void)write_register(g_runtime.icm, kPowerManagement1, 0x40);
        (void)i2c_master_bus_rm_device(g_runtime.icm);
        g_runtime.icm = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.sensor_ready = false;
    g_runtime.data = {};
    g_runtime.next_retry_ms = 0;
    g_runtime.retry_backoff_ms = 1000;
}

void task_main(void*)
{
    while (true)
    {
        bool wanted = false;
        {
            std::lock_guard<std::mutex> lock(g_runtime.mutex);
            wanted = g_runtime.pinned || g_runtime.consumers;
        }
        if (!wanted)
        {
            stop_sensor();
            bool exiting = false;
            {
                std::lock_guard<std::mutex> lock(g_runtime.mutex);
                // Re-entering during cleanup reuses this task and reprobes.
                exiting = !g_runtime.pinned && !g_runtime.consumers;
                if (exiting)
                {
                    g_runtime.task = nullptr;
                    g_runtime.started = false;
                }
            }
            if (exiting) break;
            continue;
        }
        if (ensure_sensor_ready())
        {
            if (!sample_once())
            {
                mark_stale_if_needed();
            }
        }
        else
        {
            const uint32_t now = now_ms();
            if (now - g_runtime.last_log_ms >= kLogIntervalMs)
            {
                ESP_LOGW(kTag, "Motion backend not ready; retry scheduled");
                g_runtime.last_log_ms = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
    }
    vTaskDelete(nullptr);
}

} // namespace

void ensure_started()
{
    // Compatibility for a permanent non-widget consumer. Widget users must
    // use acquire/release so leaving a page does not retain this worker.
    {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        if (g_runtime.pinned) return;
        g_runtime.pinned = true;
    }
    (void)acquire();
}

bool acquire()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    ++g_runtime.consumers;
    if (g_runtime.started)
    {
        return true;
    }
    g_runtime.started = true;
    if (xTaskCreate(task_main, "p4_heading", 4096, nullptr, 3, &g_runtime.task) != pdPASS)
    {
        g_runtime.started = false;
        g_runtime.task = nullptr;
        --g_runtime.consumers;
        g_runtime.pinned = false;
        ESP_LOGE(kTag, "Failed to create heading task");
        return false;
    }
    return true;
}

void release()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.consumers) --g_runtime.consumers;
}

HeadingState get_data()
{
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    return g_runtime.data;
}

} // namespace boards::t_display_p4::heading_runtime

#else

namespace boards::t_display_p4::heading_runtime
{

void ensure_started() {}
bool acquire() { return false; }
void release() {}

HeadingState get_data()
{
    return {};
}

} // namespace boards::t_display_p4::heading_runtime

#endif
