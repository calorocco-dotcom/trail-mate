/**
 * @file app_tasks.cpp
 * @brief Application task implementation
 */

#include "platform/esp/arduino_common/app_tasks.h"

#include "platform/radio/radio_power.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"
#if defined(ARDUINO)
#include <Arduino.h>
#include <RadioLib.h>
#else
#include "esp_timer.h"

#include <cstdio>

#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_SPI_WRITE_FAILED (-16)
#define RADIOLIB_SX126X_IRQ_RX_DONE 0x0002U
#define RADIOLIB_SX126X_IRQ_HEADER_ERR 0x0020U
#define RADIOLIB_SX126X_IRQ_CRC_ERR 0x0040U
#define RADIOLIB_SX126X_IRQ_TIMEOUT 0x0200U

static uint32_t millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

struct IdfSerialLogAdapter
{
    template <typename... Args>
    int printf(const char* format, Args... args) const
    {
        return std::printf(format, args...);
    }
};

static constexpr IdfSerialLogAdapter Serial{};
#endif
#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>

#ifndef LORA_LOG_ENABLE
#define LORA_LOG_ENABLE 0
#endif

#if LORA_LOG_ENABLE
#define LORA_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LORA_LOG(...) \
    do                \
    {                 \
    } while (0)
#endif

namespace app
{

namespace
{
constexpr TickType_t kRadioPollDelay = pdMS_TO_TICKS(10);
constexpr TickType_t kRadioForegroundPollDelay = pdMS_TO_TICKS(80);
constexpr uint32_t kRadioTaskStackBytes = 3 * 1024;
constexpr uint32_t kMeshTaskStackBytes = 8 * 1024;
constexpr uint32_t kRadioRxSummaryIntervalMs = 5000;
constexpr uint32_t kRadioForegroundRxSummaryIntervalMs = 15000;
constexpr uint32_t kRadioForegroundPostRxQuietMs = 120;
constexpr uint32_t kRadioTxRetryInitialMs = 25;
constexpr uint32_t kRadioTxRetryMaxMs = 400;

struct RadioRxSummary
{
    uint32_t packets = 0;
    uint32_t bytes = 0;
    uint32_t queue_drops = 0;
    uint32_t alloc_drops = 0;
    uint32_t read_failures = 0;
    uint32_t other_irqs = 0;
    uint32_t last_log_ms = 0;
};

bool foreground_ui_pressure_for_radio()
{
    return !::platform::ui::screen::is_sleeping() ||
           ::platform::ui::screen::is_saver_active();
}

TickType_t radio_idle_poll_delay()
{
    if (foreground_ui_pressure_for_radio())
    {
        return kRadioForegroundPollDelay;
    }
    return kRadioPollDelay;
}

bool radio_rx_quiet_window_active(uint32_t now_ms, uint32_t quiet_until_ms)
{
    return quiet_until_ms != 0 &&
           static_cast<int32_t>(quiet_until_ms - now_ms) > 0;
}

bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return deadline_ms == 0U ||
           static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

uint32_t radio_tx_retry_delay_ms(uint8_t retry_count)
{
    const uint8_t shift = retry_count > 4U ? 4U : retry_count;
    const uint32_t delay_ms = kRadioTxRetryInitialMs << shift;
    return std::min(delay_ms, kRadioTxRetryMaxMs);
}

bool transient_radio_tx_failure(int state)
{
    return state == RADIOLIB_ERR_SPI_WRITE_FAILED;
}

void drain_radio_packet_queue(QueueHandle_t queue)
{
    if (!queue)
    {
        return;
    }
    AppTasks::RadioPacket packet{};
    while (xQueueReceive(queue, &packet, 0) == pdPASS)
    {
        if (packet.data)
        {
            heap_caps_free(packet.data);
        }
    }
}

uint8_t* allocate_radio_packet_buffer(std::size_t size)
{
    const uint32_t caps =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0
            ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    return static_cast<uint8_t*>(heap_caps_malloc(size, caps));
}

uint32_t radio_rx_done_mask()
{
    uint32_t mask = 0;
#if defined(RADIOLIB_SX126X_IRQ_RX_DONE)
    mask |= RADIOLIB_SX126X_IRQ_RX_DONE;
#endif
#if defined(RADIOLIB_SX128X_IRQ_RX_DONE)
    mask |= RADIOLIB_SX128X_IRQ_RX_DONE;
#endif
#if defined(ARDUINO_LILYGO_LORA_LR1121) && defined(RADIOLIB_LR11X0_IRQ_RX_DONE)
    mask |= RADIOLIB_LR11X0_IRQ_RX_DONE;
#endif
    return mask;
}

uint32_t radio_terminal_irq_mask()
{
    uint32_t mask = radio_rx_done_mask();
#if defined(RADIOLIB_SX126X_IRQ_CRC_ERR)
    mask |= RADIOLIB_SX126X_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_SX126X_IRQ_HEADER_ERR)
    mask |= RADIOLIB_SX126X_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_SX126X_IRQ_TIMEOUT)
    mask |= RADIOLIB_SX126X_IRQ_TIMEOUT;
#endif
#if defined(RADIOLIB_SX128X_IRQ_CRC_ERR)
    mask |= RADIOLIB_SX128X_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_SX128X_IRQ_HEADER_ERR)
    mask |= RADIOLIB_SX128X_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_SX128X_IRQ_TIMEOUT)
    mask |= RADIOLIB_SX128X_IRQ_TIMEOUT;
#endif
#if defined(ARDUINO_LILYGO_LORA_LR1121)
#if defined(RADIOLIB_LR11X0_IRQ_CRC_ERR)
    mask |= RADIOLIB_LR11X0_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_LR11X0_IRQ_HEADER_ERR)
    mask |= RADIOLIB_LR11X0_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_LR11X0_IRQ_TIMEOUT)
    mask |= RADIOLIB_LR11X0_IRQ_TIMEOUT;
#endif
#endif
    return mask;
}

bool radio_read_state_has_payload(int state)
{
    if (state == RADIOLIB_ERR_NONE)
    {
        return true;
    }
#if defined(RADIOLIB_ERR_CRC_MISMATCH)
    // RadioLib drivers can report CRC mismatch after the FIFO payload has
    // already been copied. Let protocol-level parsers/signatures decide
    // whether the bytes are usable instead of dropping them before the adapter.
    return state == RADIOLIB_ERR_CRC_MISMATCH;
#else
    return false;
#endif
}

void maybe_log_radio_rx_summary(RadioRxSummary& summary)
{
#if LORA_LOG_ENABLE
    const uint32_t now_ms = millis();
    const uint32_t interval_ms = foreground_ui_pressure_for_radio()
                                     ? kRadioForegroundRxSummaryIntervalMs
                                     : kRadioRxSummaryIntervalMs;
    if (summary.last_log_ms == 0)
    {
        summary.last_log_ms = now_ms;
    }
    if ((now_ms - summary.last_log_ms) < interval_ms)
    {
        return;
    }
    if (summary.packets != 0 || summary.queue_drops != 0 || summary.alloc_drops != 0 ||
        summary.read_failures != 0 || summary.other_irqs != 0)
    {
        Serial.printf("[LORA] RX stats packets=%lu bytes=%lu queue_drop=%lu alloc_drop=%lu read_fail=%lu other_irq=%lu\n",
                      static_cast<unsigned long>(summary.packets),
                      static_cast<unsigned long>(summary.bytes),
                      static_cast<unsigned long>(summary.queue_drops),
                      static_cast<unsigned long>(summary.alloc_drops),
                      static_cast<unsigned long>(summary.read_failures),
                      static_cast<unsigned long>(summary.other_irqs));
    }
    summary = RadioRxSummary{};
    summary.last_log_ms = now_ms;
#else
    (void)summary;
#endif
}
} // namespace

// Static members
QueueHandle_t AppTasks::radio_tx_queue_ = nullptr;
QueueHandle_t AppTasks::radio_rx_queue_ = nullptr;
QueueHandle_t AppTasks::mesh_queue_ = nullptr;
TaskHandle_t AppTasks::radio_task_handle_ = nullptr;
TaskHandle_t AppTasks::mesh_task_handle_ = nullptr;
LoraBoard* AppTasks::board_ = nullptr;
chat::IMeshAdapter* AppTasks::adapter_ = nullptr;
IRawRadioPacketInterceptor* AppTasks::raw_radio_packet_interceptor_ = nullptr;
uint8_t* AppTasks::radio_rx_scratch_ = nullptr;
volatile bool AppTasks::radio_tasks_paused_ = false;
volatile bool AppTasks::radio_receive_active_ = false;
volatile bool AppTasks::radio_receive_restart_pending_ = true;
volatile bool AppTasks::radio_receive_suppressed_ = false;
volatile bool AppTasks::radio_transmit_active_ = false;
volatile bool AppTasks::radio_task_quiesced_ = false;
volatile bool AppTasks::mesh_task_quiesced_ = false;

bool AppTasks::init(LoraBoard& board, chat::IMeshAdapter* adapter)
{
    board_ = &board;
    adapter_ = adapter;
    radio_tasks_paused_ = false;
    radio_task_quiesced_ = false;
    mesh_task_quiesced_ = false;
    radio_receive_active_ = false;
    radio_receive_restart_pending_ = !radio_receive_suppressed_;
    (void)platform::radio::power_policy().request(platform::ui::settings_store::get_bool("settings", "radio_enabled", true));
    if (!radio_rx_scratch_)
    {
        radio_rx_scratch_ = allocate_radio_packet_buffer(255U);
    }
    if (!radio_rx_scratch_)
    {
        Serial.printf("[LORA] RX scratch allocation failed bytes=255\n");
        return false;
    }

    // Create queues
    radio_tx_queue_ = xQueueCreate(RADIO_QUEUE_SIZE, sizeof(RadioPacket));
    radio_rx_queue_ = xQueueCreate(RADIO_QUEUE_SIZE, sizeof(RadioPacket));
    mesh_queue_ = xQueueCreate(MESH_QUEUE_SIZE, sizeof(RadioPacket));

    if (!radio_tx_queue_ || !radio_rx_queue_ || !mesh_queue_)
    {
        return false;
    }

    // Create radio task (high priority)
    BaseType_t result = xTaskCreate(
        radioTask,
        "radio_task",
        kRadioTaskStackBytes,
        nullptr,
        10, // High priority
        &radio_task_handle_);

    if (result != pdPASS)
    {
        return false;
    }

    // Create mesh task (medium priority)
    result = xTaskCreate(
        meshTask,
        "mesh_task",
        kMeshTaskStackBytes,
        nullptr,
        5, // Medium priority
        &mesh_task_handle_);

    return (result == pdPASS);
}

bool AppTasks::pauseRadioTasks(uint32_t timeout_ms)
{
    if (radio_tasks_paused_)
    {
        return (radio_task_handle_ == nullptr || radio_task_quiesced_) &&
               (mesh_task_handle_ == nullptr || mesh_task_quiesced_);
    }
    const TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task == radio_task_handle_ || current_task == mesh_task_handle_)
    {
        Serial.printf("[LORA] task quiesce rejected caller=owned_task\n");
        return false;
    }

    if (!platform::radio::power_policy().begin_exclusive()) return false;
    radio_tasks_paused_ = true;
    requestRadioReceiveRestart();
    const uint32_t start_ms = millis();
    while ((radio_task_handle_ != nullptr && !radio_task_quiesced_) ||
           (mesh_task_handle_ != nullptr && !mesh_task_quiesced_))
    {
        if (timeout_ms != 0U && millis() - start_ms >= timeout_ms)
        {
            radio_tasks_paused_ = false;
            platform::radio::power_policy().end_exclusive();
            Serial.printf("[LORA] task quiesce timeout radio=%u mesh=%u wait_ms=%lu\n",
                          radio_task_quiesced_ ? 1U : 0U,
                          mesh_task_quiesced_ ? 1U : 0U,
                          static_cast<unsigned long>(millis() - start_ms));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (radio_tx_queue_)
    {
        drain_radio_packet_queue(radio_tx_queue_);
    }
    if (radio_rx_queue_)
    {
        drain_radio_packet_queue(radio_rx_queue_);
    }
    if (mesh_queue_)
    {
        drain_radio_packet_queue(mesh_queue_);
    }
    Serial.printf("[LORA] tasks quiesced wait_ms=%lu\n",
                  static_cast<unsigned long>(millis() - start_ms));
    return true;
}

bool AppTasks::quiesceRadioHardwareForExternalStorage()
{
    const bool radio_quiesced =
        radio_task_handle_ == nullptr || radio_task_quiesced_;
    const bool mesh_quiesced =
        mesh_task_handle_ == nullptr || mesh_task_quiesced_;
    if (!radio_tasks_paused_ || !radio_quiesced || !mesh_quiesced)
    {
        Serial.printf("[LORA] external-storage standby rejected paused=%u radio=%u mesh=%u\n",
                      radio_tasks_paused_ ? 1U : 0U,
                      radio_quiesced ? 1U : 0U,
                      mesh_quiesced ? 1U : 0U);
        return false;
    }

    if (board_ == nullptr)
    {
        return true;
    }

    const bool quiesced = board_->quiesceForExternalStorage();
    Serial.printf("[LORA] external-storage standby=%u\n", quiesced ? 1U : 0U);
    return quiesced;
}

void AppTasks::resumeRadioTasks()
{
    if (!radio_tasks_paused_)
    {
        return;
    }
    radio_tasks_paused_ = false;

    requestRadioReceiveRestart();
    platform::radio::power_policy().end_exclusive();
}

void AppTasks::setRadioReceiveActive(bool active)
{
    if (radio_receive_suppressed_ && active)
    {
        radio_receive_active_ = false;
        radio_receive_restart_pending_ = false;
        return;
    }
    radio_receive_active_ = active;
    radio_receive_restart_pending_ = !active;
    if (active) platform::radio::power_policy().acknowledge(true);
}

void AppTasks::requestRadioReceiveRestart()
{
    if (radio_receive_suppressed_)
    {
        radio_receive_active_ = false;
        radio_receive_restart_pending_ = false;
        return;
    }
    radio_receive_active_ = false;
    radio_receive_restart_pending_ = true;
}

void AppTasks::setRadioReceiveSuppressed(bool suppressed)
{
    if (radio_receive_suppressed_ == suppressed)
    {
        return;
    }
    radio_receive_suppressed_ = suppressed;
    radio_receive_active_ = false;
    radio_receive_restart_pending_ = !suppressed;
    Serial.printf("[LORA] RX gate %s\n", suppressed ? "suppressed" : "enabled");
}

bool AppTasks::isRadioReceiveSuppressed()
{
    return radio_receive_suppressed_;
}

void AppTasks::setRadioTransmitActive(bool active)
{
    radio_transmit_active_ = active;
    if (active)
    {
        radio_receive_active_ = false;
        radio_receive_restart_pending_ = false;
    }
    else
    {
        radio_receive_restart_pending_ = !radio_receive_suppressed_;
    }
}

bool AppTasks::isRadioTransmitActive()
{
    return radio_transmit_active_;
}

bool AppTasks::enqueueRadioTransmit(const uint8_t* data, size_t size)
{
    if (!platform::radio::power_policy().enabled() || !data || size == 0 || size > 255 || !radio_tx_queue_)
    {
        return false;
    }

    uint8_t* copy = allocate_radio_packet_buffer(size);
    if (!copy)
    {
        return false;
    }
    std::memcpy(copy, data, size);

    RadioPacket packet{};
    packet.data = copy;
    packet.size = size;
    packet.is_tx = true;
    packet.queued_ms = millis();
    if (xQueueSend(radio_tx_queue_, &packet, 0) != pdPASS)
    {
        heap_caps_free(copy);
        LORA_LOG("[LORA] TX queue full len=%u\n", static_cast<unsigned>(size));
        return false;
    }

    requestRadioReceiveRestart();
    return true;
}

void AppTasks::setRawRadioPacketInterceptor(
    IRawRadioPacketInterceptor* interceptor)
{
    raw_radio_packet_interceptor_ = interceptor;
}

AppTasks::ScopedRadioTransmitActivity::ScopedRadioTransmitActivity()
{
    AppTasks::setRadioTransmitActive(true);
}

AppTasks::ScopedRadioTransmitActivity::~ScopedRadioTransmitActivity()
{
    AppTasks::setRadioTransmitActive(false);
}

void AppTasks::radioTask(void* pvParameters)
{
    (void)pvParameters;

    const uint32_t rx_done_mask = radio_rx_done_mask();
    const uint32_t terminal_irq_mask = radio_terminal_irq_mask();
    RadioRxSummary rx_summary{};
    uint32_t foreground_rx_quiet_until_ms = 0;
    uint32_t next_standby_attempt_ms = 0;

    while (true)
    {
        if (radio_tasks_paused_)
        {
            radio_task_quiesced_ = true;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        radio_task_quiesced_ = false;

        auto& power = platform::radio::power_policy();
        if (!power.enabled())
        {
            const auto status = power.status();
            if (status.applied && deadline_reached(millis(), next_standby_attempt_ms))
            {
                // Complete any in-flight TX before this point. Reuse the board's
                // checked standby operation; do not cut shared SD/display rails.
                if (!board_ || board_->quiesceForExternalStorage()) power.acknowledge(false);
                else power.fail();
                next_standby_attempt_ms = millis() + 1000;
                radio_receive_active_ = false;
                radio_receive_restart_pending_ = false;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (!power.status().applied)
        {
            if (!board_ || !board_->isRadioOnline())
            {
                power.fail();
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            requestRadioReceiveRestart();
            // RX suppression is a protocol policy, independent of RF permission.
            if (radio_receive_suppressed_ && board_ && board_->isRadioOnline()) power.acknowledge(true);
        }

        if (::platform::ui::reticulum_call::realtime_mode_active())
        {
            requestRadioReceiveRestart();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bool should_restart_rx = radio_receive_restart_pending_;
        bool handled_tx = false;
        bool deferred_tx = false;

        if (radio_transmit_active_)
        {
            vTaskDelay(kRadioPollDelay);
            continue;
        }

        // Process TX queue
        RadioPacket tx_packet{};
        const uint32_t tx_now_ms = millis();
        RadioPacket queued_packet{};
        const bool tx_ready =
            xQueuePeek(radio_tx_queue_, &queued_packet, 0) == pdPASS &&
            deadline_reached(tx_now_ms, queued_packet.next_attempt_ms);
        if (tx_ready &&
            xQueueReceive(radio_tx_queue_, &tx_packet, 0) == pdPASS)
        {
            handled_tx = tx_packet.is_tx && tx_packet.data && tx_packet.size > 0;
            if (tx_packet.is_tx && tx_packet.data && tx_packet.size > 0)
            {
                bool keep_packet = false;
                // Send packet
                if (board_ && board_->isRadioOnline())
                {
                    requestRadioReceiveRestart();
                    int state = RADIOLIB_ERR_NONE;
                    setRadioTransmitActive(true);
                    state = board_->transmitRadio(tx_packet.data, tx_packet.size);
                    setRadioTransmitActive(false);
                    LORA_LOG("[LORA] TX queue len=%u state=%d\n", (unsigned)tx_packet.size, state);
                    if (transient_radio_tx_failure(state))
                    {
                        if (tx_packet.retry_count != UINT8_MAX)
                        {
                            ++tx_packet.retry_count;
                        }
                        tx_packet.next_attempt_ms =
                            millis() + radio_tx_retry_delay_ms(tx_packet.retry_count);
                        if (xQueueSendToFront(radio_tx_queue_, &tx_packet, 0) == pdPASS)
                        {
                            keep_packet = true;
                            deferred_tx = true;
                            LORA_LOG("[LORA] TX deferred len=%u state=%d retry=%u age_ms=%lu\n",
                                     static_cast<unsigned>(tx_packet.size),
                                     state,
                                     static_cast<unsigned>(tx_packet.retry_count),
                                     static_cast<unsigned long>(millis() - tx_packet.queued_ms));
                            if (tx_packet.retry_count == 1U ||
                                (tx_packet.retry_count % 16U) == 0U)
                            {
                                Serial.printf("[LORA] TX deferred reason=spi_busy len=%u retry=%u age_ms=%lu\n",
                                              static_cast<unsigned>(tx_packet.size),
                                              static_cast<unsigned>(tx_packet.retry_count),
                                              static_cast<unsigned long>(millis() - tx_packet.queued_ms));
                            }
                        }
                        else
                        {
                            Serial.printf("[LORA] TX retry queue lost len=%u state=%d retry=%u\n",
                                          static_cast<unsigned>(tx_packet.size),
                                          state,
                                          static_cast<unsigned>(tx_packet.retry_count));
                        }
                    }
                    else if (state == RADIOLIB_ERR_NONE &&
                             tx_packet.retry_count != 0U)
                    {
                        Serial.printf("[LORA] TX recovered len=%u retries=%u age_ms=%lu\n",
                                      static_cast<unsigned>(tx_packet.size),
                                      static_cast<unsigned>(tx_packet.retry_count),
                                      static_cast<unsigned long>(millis() - tx_packet.queued_ms));
                    }
                    else if (state != RADIOLIB_ERR_NONE)
                    {
                        Serial.printf("[LORA] TX failed len=%u state=%d age_ms=%lu\n",
                                      static_cast<unsigned>(tx_packet.size),
                                      state,
                                      static_cast<unsigned long>(millis() - tx_packet.queued_ms));
                    }
                    if (state == RADIOLIB_ERR_NONE && !radio_receive_suppressed_)
                    {
                        int rx_state = board_->startRadioReceive();
                        if (rx_state == RADIOLIB_ERR_NONE)
                        {
                            setRadioReceiveActive(true);
                            should_restart_rx = false;
                        }
                        else
                        {
                            requestRadioReceiveRestart();
                            LORA_LOG("[LORA] RX start fail state=%d\n", rx_state);
                        }
                    }
                    else if (state == RADIOLIB_ERR_NONE)
                    {
                        radio_receive_active_ = false;
                        radio_receive_restart_pending_ = false;
                    }
                    else
                    {
                        requestRadioReceiveRestart();
                    }
                }
                else
                {
                    LORA_LOG("[LORA] TX drop (radio offline) len=%u\n", (unsigned)tx_packet.size);
                }
                if (!keep_packet)
                {
                    heap_caps_free(tx_packet.data);
                }
            }
        }

        if (deferred_tx)
        {
            maybe_log_radio_rx_summary(rx_summary);
            vTaskDelay(kRadioPollDelay);
            continue;
        }

        if (radio_receive_suppressed_)
        {
            radio_receive_active_ = false;
            radio_receive_restart_pending_ = false;
            maybe_log_radio_rx_summary(rx_summary);
            vTaskDelay(kRadioPollDelay);
            continue;
        }

        if (!handled_tx && foreground_ui_pressure_for_radio() &&
            radio_rx_quiet_window_active(millis(), foreground_rx_quiet_until_ms))
        {
            maybe_log_radio_rx_summary(rx_summary);
            vTaskDelay(kRadioForegroundPollDelay);
            continue;
        }

        // Poll for RX (non-blocking)
        if (board_ && board_->isRadioOnline())
        {
            if (!radio_receive_active_ || radio_receive_restart_pending_ || should_restart_rx)
            {
                int rx_state = board_->startRadioReceive();
                if (rx_state == RADIOLIB_ERR_NONE)
                {
                    setRadioReceiveActive(true);
                    should_restart_rx = false;
                }
                else
                {
                    requestRadioReceiveRestart();
                    LORA_LOG("[LORA] RX start fail state=%d\n", rx_state);
                }
            }
            // Check if data available using RadioLib IRQs
            int packet_length = 0;
            bool handled_rx_activity = false;
            uint32_t irq = board_->getRadioIrqFlags();
            if ((irq & rx_done_mask) != 0)
            {
                handled_rx_activity = true;
                packet_length = static_cast<int>(board_->getRadioPacketLength(true));
                if (packet_length > 0 && packet_length <= 255)
                {
                    int state = board_->readRadioData(radio_rx_scratch_, packet_length);
                    if (radio_read_state_has_payload(state))
                    {
                        RadioPacket rx_packet;
                        rx_packet.data =
                            allocate_radio_packet_buffer(packet_length);
                        if (rx_packet.data)
                        {
                            memcpy(rx_packet.data,
                                   radio_rx_scratch_,
                                   packet_length);
                            rx_packet.size = packet_length;
                            rx_packet.is_tx = false;
                            rx_packet.rssi = board_->getRadioRSSI();
                            rx_packet.snr = board_->getRadioSNR();

                            ++rx_summary.packets;
                            rx_summary.bytes += static_cast<uint32_t>(packet_length);
                            if (xQueueSend(mesh_queue_, &rx_packet, 0) != pdPASS)
                            {
                                heap_caps_free(rx_packet.data);
                                ++rx_summary.queue_drops;
                            }
                        }
                        else
                        {
                            ++rx_summary.alloc_drops;
                        }
                    }
                    else
                    {
                        ++rx_summary.read_failures;
                        board_->clearRadioIrqFlags(irq);
                    }
                }
                board_->clearRadioIrqFlags(irq);
                should_restart_rx = true;
            }
            else if (irq)
            {
                handled_rx_activity = true;
                ++rx_summary.other_irqs;
                board_->clearRadioIrqFlags(irq);
                if ((irq & terminal_irq_mask) != 0)
                {
                    should_restart_rx = true;
                }
            }
            if (packet_length > 0 || should_restart_rx)
            {
                requestRadioReceiveRestart();
                int rx_state = board_->startRadioReceive();
                if (rx_state == RADIOLIB_ERR_NONE)
                {
                    setRadioReceiveActive(true);
                }
                else
                {
                    requestRadioReceiveRestart();
                    LORA_LOG("[LORA] RX restart fail state=%d\n", rx_state);
                }
            }
            if (handled_rx_activity && foreground_ui_pressure_for_radio())
            {
                foreground_rx_quiet_until_ms = millis() + kRadioForegroundPostRxQuietMs;
            }
            maybe_log_radio_rx_summary(rx_summary);
        }

        vTaskDelay(radio_idle_poll_delay());
    }
}

void AppTasks::meshTask(void* pvParameters)
{
    (void)pvParameters;

    const TickType_t poll_delay = pdMS_TO_TICKS(50);

    while (true)
    {
        if (radio_tasks_paused_)
        {
            mesh_task_quiesced_ = true;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        mesh_task_quiesced_ = false;

        // Process received packets
        RadioPacket rx_packet;
        if (::platform::ui::reticulum_call::realtime_mode_active())
        {
            while (xQueueReceive(mesh_queue_, &rx_packet, 0) == pdPASS)
            {
                if (rx_packet.data)
                {
                    heap_caps_free(rx_packet.data);
                }
            }
            if (adapter_)
            {
                adapter_->processSendQueue();
            }
            vTaskDelay(poll_delay);
            continue;
        }
        if (xQueueReceive(mesh_queue_, &rx_packet, 0) == pdPASS)
        {
            if (!rx_packet.is_tx && rx_packet.data)
            {
                const bool intercepted = raw_radio_packet_interceptor_ &&
                                         raw_radio_packet_interceptor_->tryConsume(
                                             rx_packet.data,
                                             rx_packet.size,
                                             rx_packet.rssi,
                                             rx_packet.snr);
                if (!intercepted && adapter_)
                {
                    // The default behavior is intentionally unchanged for any
                    // frame that an application-owned sideband parser declines.
                    adapter_->setLastRxStats(rx_packet.rssi, rx_packet.snr);
                    adapter_->handleRawPacket(rx_packet.data, rx_packet.size);
                }

                // Both the mesh adapter and a sideband parser receive borrowed
                // bytes only; AppTasks retains and releases this allocation.
                heap_caps_free(rx_packet.data);
            }
        }

        // Process send queue in adapter
        if (adapter_)
        {
            adapter_->processSendQueue();
        }

        vTaskDelay(poll_delay);
    }
}

} // namespace app
