#include "platform/esp/idf_common/wireless_companion/c6_companion.h"

#include "hostlink/c6/c6_frame_codec.h"
#include "hostlink/c6/c6_protocol.h"
#include "platform/memory/psram_ptr.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_runtime.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_serial_slave_link/essl.h"
#include "esp_serial_slave_link/essl_sdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/sdmmc_host_runtime.h"
#include "sdmmc_cmd.h"
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "boards/tab5/tab5_board.h"
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/t_display_p4_board.h"
#endif
#endif

namespace platform::esp::idf_common::wireless_companion
{
namespace
{

constexpr const char* kTag = "P4_C6";
constexpr uint16_t kInitialSequence = 1;
constexpr uint32_t kHandshakeTimeoutMs = 500;
constexpr uint32_t kShortIoTimeoutMs = 100;
constexpr uint32_t kReceiveRetryDelayMs = 10;
constexpr uint32_t kReceivePollTimeoutMs = 20;
constexpr uint32_t kSdioCardInitRetryDelayMs = 100;
constexpr uint32_t kSdioCardInitMaxAttempts = 30;
constexpr size_t kSdioBufferSize = 1152;
constexpr size_t kC6MaxFrameSize = TM_C6_FRAME_HEADER_LEN + TM_C6_MAX_PAYLOAD;
constexpr size_t kC6RxStreamCapacity = kSdioBufferSize + kC6MaxFrameSize;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
constexpr size_t kWifiTcpRxBufferSize = 16384;
#else
constexpr size_t kWifiTcpRxBufferSize = 4096;
#endif
constexpr uint32_t kRequestedFeatures = TM_C6_FEATURE_ESPNOW_TEAM |
                                        TM_C6_FEATURE_WIFI_STA |
                                        TM_C6_FEATURE_WIFI_AP |
                                        TM_C6_FEATURE_DIAG_LOG |
                                        TM_C6_FEATURE_HOSTLINK_PING |
                                        TM_C6_FEATURE_WIFI_TCP_PROXY;
constexpr uint32_t kDefaultConfigSequence = 1;
constexpr const char* kSettingsNamespace = "c6_companion";
constexpr uint32_t kNetworkTimeMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC
bool board_has_c6_companion()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return true;
#else
    return false;
#endif
}

#if defined(ESP_PLATFORM) && defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
bool t_display_p4_enter_c6_download_mode(const char** out_detail)
{
    auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
    const auto& profile = ::boards::t_display_p4::TDisplayP4Board::profile();
    const auto& io = profile.io_expander;
    const bool boot_active_high = !profile.c6_boot_active_low;

    auto fail = [&](const char* detail) -> bool
    {
        if (out_detail != nullptr)
        {
            *out_detail = detail;
        }
        ESP_LOGW(kTag,
                 "C6 download mode failed detail=%s boot_pin=%d reset_pin=%d",
                 detail ? detail : "unknown",
                 io.c6_boot,
                 io.c6_enable);
        return false;
    };

    if (io.c6_boot < 0)
    {
        return fail("c6_boot_pin_missing");
    }
    if (io.c6_enable < 0)
    {
        return fail("c6_reset_pin_missing");
    }
    if (!board.expanderPinMode(io.c6_boot, true))
    {
        return fail("c6_boot_pin_mode_failed");
    }
    if (!board.expanderPinMode(io.c6_enable, true))
    {
        return fail("c6_reset_pin_mode_failed");
    }

    ESP_LOGI(kTag,
             "C6 download mode sequence begin boot_pin=%d boot_active_low=%u reset_pin=%d reset_release_active_high=%u",
             io.c6_boot,
             profile.c6_boot_active_low ? 1u : 0u,
             io.c6_enable,
             profile.c6_enable_active_high ? 1u : 0u);

    if (!board.expanderWriteActive(io.c6_enable, true, profile.c6_enable_active_high))
    {
        return fail("c6_reset_release_failed");
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    if (!board.expanderWriteActive(io.c6_boot, true, boot_active_high))
    {
        return fail("c6_boot_assert_failed");
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    if (!board.expanderWriteActive(io.c6_enable, false, profile.c6_enable_active_high))
    {
        (void)board.expanderWriteActive(io.c6_boot, false, boot_active_high);
        return fail("c6_reset_assert_failed");
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!board.expanderWriteActive(io.c6_enable, true, profile.c6_enable_active_high))
    {
        (void)board.expanderWriteActive(io.c6_boot, false, boot_active_high);
        return fail("c6_reset_release_failed");
    }
    vTaskDelay(pdMS_TO_TICKS(250));

    if (!board.expanderWriteActive(io.c6_boot, false, boot_active_high))
    {
        return fail("c6_boot_release_failed");
    }
    (void)board.expanderPinMode(io.c6_boot, false);

    if (out_detail != nullptr)
    {
        *out_detail = "download_mode_requested";
    }
    ESP_LOGI(kTag,
             "C6 download mode sequence complete boot_pin=%d reset_pin=%d",
             io.c6_boot,
             io.c6_enable);
    return true;
}
#endif

uint16_t read_le16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read_le32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void write_le16(uint8_t* data, uint16_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFFu);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void write_le32(uint8_t* data, uint32_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFFu);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint8_t bool_byte(bool value)
{
    return value ? 1u : 0u;
}

template <size_t N>
void copy_text(char (&out)[N], const char* text)
{
    out[0] = '\0';
    if (!text)
    {
        return;
    }
    const size_t max_copy_len = N - 1U;
    const auto* terminator = static_cast<const char*>(std::memchr(text, '\0', max_copy_len));
    const size_t copy_len =
        terminator ? static_cast<size_t>(terminator - text) : max_copy_len;
    std::memcpy(out, text, copy_len);
    out[copy_len] = '\0';
}

template <size_t N>
void copy_text(char (&out)[N], const std::string& text)
{
    copy_text(out, text.c_str());
}

void apply_c6_wifi_time_sync(const tm_c6_wifi_time_sync_t& event)
{
#if defined(ESP_PLATFORM)
    char source[TM_C6_WIFI_TIME_SOURCE_LEN + 1] = {};
    const auto* source_end =
        static_cast<const char*>(std::memchr(event.source, '\0', TM_C6_WIFI_TIME_SOURCE_LEN));
    const std::size_t source_len =
        source_end ? static_cast<std::size_t>(source_end - event.source) : TM_C6_WIFI_TIME_SOURCE_LEN;
    std::memcpy(source, event.source, source_len);
    source[source_len] = '\0';
    const char* label = source[0] != '\0' ? source : "c6_wifi_sntp";

    if (event.error_code != TM_C6_OK)
    {
        ESP_LOGW(kTag,
                 "C6 Wi-Fi time sync failed source=%s err=%u",
                 label,
                 static_cast<unsigned>(event.error_code));
        return;
    }
    if (event.epoch_seconds < kNetworkTimeMinValidEpochSeconds)
    {
        ESP_LOGW(kTag,
                 "C6 Wi-Fi time sync invalid epoch=%lu source=%s",
                 static_cast<unsigned long>(event.epoch_seconds),
                 label);
        return;
    }

    const bool applied =
        ::platform::esp::boards::applySystemTimeAndSyncBoardRtc(static_cast<std::time_t>(event.epoch_seconds), label);
    ESP_LOGI(kTag,
             "C6 Wi-Fi time sync epoch=%lu source=%s rtc=%s",
             static_cast<unsigned long>(event.epoch_seconds),
             label,
             applied ? "updated" : "update_failed");
#else
    (void)event;
#endif
}

void set_detail(C6CompanionStatus& status, CompanionState state, const char* detail)
{
    status.present = state == CompanionState::Present;
    status.state = state;
    status.detail = detail;
}

BleCompanionConfig load_ble_config_from_p4_settings()
{
    // BLE application bridges are deliberately outside the supported P4/C6
    // product surface. Ignore stale persisted BLE settings so an older NVS
    // image cannot silently re-enable Meshtastic or MeshCore advertising.
    return {};
}

EspNowCompanionConfig load_espnow_config_from_p4_settings()
{
    EspNowCompanionConfig config{};
#if defined(ESP_PLATFORM)
    config.enabled = ::platform::ui::settings_store::get_bool(kSettingsNamespace, "espnow_enabled", config.enabled);
    config.team_discovery_enabled = ::platform::ui::settings_store::get_bool(
        kSettingsNamespace,
        "espnow_team_discovery",
        config.team_discovery_enabled);
    const int channel = ::platform::ui::settings_store::get_int(kSettingsNamespace, "espnow_channel", config.channel);
    if (channel >= 0 && channel <= 14)
    {
        config.channel = static_cast<uint8_t>(channel);
    }
    const int beacon_interval = ::platform::ui::settings_store::get_int(
        kSettingsNamespace,
        "espnow_beacon_interval_ms",
        static_cast<int>(config.beacon_interval_ms));
    if (beacon_interval > 0 && beacon_interval <= 60000)
    {
        config.beacon_interval_ms = static_cast<uint16_t>(beacon_interval);
    }
#endif
    return config;
}

WifiCompanionConfig load_wifi_config_from_p4_settings()
{
    WifiCompanionConfig config{};
#if defined(ESP_PLATFORM)
    ::platform::ui::wifi::Config shared_wifi{};
    (void)::platform::ui::wifi::load_config(shared_wifi);
    config.enabled =
        ::platform::ui::settings_store::get_bool(kSettingsNamespace,
                                                 "wifi_enabled",
                                                 shared_wifi.enabled);
    config.sta_enabled =
        ::platform::ui::settings_store::get_bool(kSettingsNamespace, "wifi_sta_enabled", config.enabled);
    config.ap_enabled =
        ::platform::ui::settings_store::get_bool(kSettingsNamespace, "wifi_ap_enabled", config.ap_enabled);
    config.persist_credentials =
        ::platform::ui::settings_store::get_bool(kSettingsNamespace, "wifi_persist_credentials", false);

    std::string value;
    if (::platform::ui::settings_store::get_string(kSettingsNamespace,
                                                   "wifi_sta_ssid",
                                                   value))
    {
        copy_text(config.sta_ssid, value);
    }
    else
    {
        copy_text(config.sta_ssid, shared_wifi.ssid);
    }
    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNamespace,
                                                   "wifi_sta_password",
                                                   value))
    {
        copy_text(config.sta_password, value);
    }
    else
    {
        copy_text(config.sta_password, shared_wifi.password);
    }
    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNamespace, "wifi_ap_ssid", value))
    {
        copy_text(config.ap_ssid, value);
    }
    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNamespace, "wifi_ap_password", value))
    {
        copy_text(config.ap_password, value);
    }

    if (shared_wifi.enabled && config.sta_ssid[0] != '\0')
    {
        config.enabled = true;
        config.sta_enabled = true;
    }

    const int sta_channel = ::platform::ui::settings_store::get_int(kSettingsNamespace, "wifi_sta_channel", 0);
    if (sta_channel >= 0 && sta_channel <= 14)
    {
        config.sta_channel = static_cast<uint8_t>(sta_channel);
    }
    const int ap_channel = ::platform::ui::settings_store::get_int(kSettingsNamespace, "wifi_ap_channel", config.ap_channel);
    if (ap_channel > 0 && ap_channel <= 14)
    {
        config.ap_channel = static_cast<uint8_t>(ap_channel);
    }
#endif
    return config;
}

uint32_t requested_features_for(const BleCompanionConfig& ble,
                                const EspNowCompanionConfig& espnow,
                                const WifiCompanionConfig& wifi)
{
    uint32_t features = TM_C6_FEATURE_DIAG_LOG | TM_C6_FEATURE_HOSTLINK_PING;
    if (ble.enabled)
    {
        if (ble.meshtastic_enabled)
        {
            features |= TM_C6_FEATURE_BLE_MESHTASTIC;
        }
        if (ble.meshcore_enabled)
        {
            features |= TM_C6_FEATURE_BLE_MESHCORE;
        }
        if (ble.trailmate_enabled)
        {
            features |= TM_C6_FEATURE_BLE_TRAILMATE;
        }
    }
    if (espnow.enabled)
    {
        features |= TM_C6_FEATURE_ESPNOW_TEAM;
    }
    if (wifi.enabled)
    {
        if (wifi.sta_enabled)
        {
            features |= TM_C6_FEATURE_WIFI_STA | TM_C6_FEATURE_WIFI_TCP_PROXY;
        }
        if (wifi.ap_enabled)
        {
            features |= TM_C6_FEATURE_WIFI_AP;
        }
    }
    return features & kRequestedFeatures;
}

uint8_t channel_for_ble_profile(BleProfile profile)
{
    switch (profile)
    {
    case BleProfile::Meshtastic:
        return TM_C6_CH_BLE_MESHTASTIC;
    case BleProfile::MeshCore:
        return TM_C6_CH_BLE_MESHCORE;
    case BleProfile::TrailMate:
        return TM_C6_CH_BLE_TRAILMATE;
    case BleProfile::None:
        break;
    }
    return TM_C6_CH_CONTROL;
}

#if defined(ESP_PLATFORM)
class C6Transport
{
  public:
    virtual bool begin() = 0;
    virtual bool send(const uint8_t* data, size_t len, uint32_t timeout_ms) = 0;
    virtual bool recv(uint8_t* data, size_t max_len, size_t& out_len, uint32_t timeout_ms) = 0;
    virtual void reset() = 0;
    virtual const char* last_error() const = 0;
    virtual uint32_t last_rx_size() const = 0;
    virtual int last_rx_error() const = 0;
    virtual size_t last_size_read() const = 0;
    virtual ~C6Transport() = default;
};

class SdioC6Transport final : public C6Transport
{
  public:
    ~SdioC6Transport() override
    {
        reset();
    }

    bool begin() override
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (ready_)
        {
            return true;
        }

        if (!prepare_board_companion())
        {
            return false;
        }

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_1;
        host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF |
                     SDMMC_HOST_FLAG_DEINIT_ARG;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 4;
        apply_board_pins(slot_config);
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_err_t err = sdmmc_host_runtime::initialize_slot(
            sdmmc_host_runtime::SlotOwner::C6Companion, host, slot_config);
        if (err != ESP_OK)
        {
            set_error("sdmmc_slot_init_failed");
            ESP_LOGW(kTag, "SDIO slot init failed: %s", esp_err_to_name(err));
            deinit_locked();
            return false;
        }
        slot_initialized_ = true;

        card_ = static_cast<sdmmc_card_t*>(calloc(1, sizeof(sdmmc_card_t)));
        if (card_ == nullptr)
        {
            set_error("sdio_card_alloc_failed");
            deinit_locked();
            return false;
        }

        for (uint32_t attempt = 1; attempt <= kSdioCardInitMaxAttempts; ++attempt)
        {
            std::memset(card_, 0, sizeof(*card_));
            err = sdmmc_card_init(&host, card_);
            if (err == ESP_OK)
            {
                break;
            }
            if (attempt < kSdioCardInitMaxAttempts)
            {
                ESP_LOGW(kTag,
                         "SDIO card init attempt %lu/%lu failed: %s; retrying",
                         static_cast<unsigned long>(attempt),
                         static_cast<unsigned long>(kSdioCardInitMaxAttempts),
                         esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(kSdioCardInitRetryDelayMs));
            }
        }
        if (err != ESP_OK)
        {
            set_error("sdio_card_init_failed");
            ESP_LOGW(kTag, "SDIO card init failed: %s", esp_err_to_name(err));
            deinit_locked();
            return false;
        }
        ESP_LOGI(kTag,
                 "SDIO card detected is_sdio=%lu is_mem=%lu functions=%lu ocr=0x%08lx rca=0x%04x real_freq_khz=%d",
                 static_cast<unsigned long>(card_->is_sdio),
                 static_cast<unsigned long>(card_->is_mem),
                 static_cast<unsigned long>(card_->num_io_functions),
                 static_cast<unsigned long>(card_->ocr),
                 static_cast<unsigned>(card_->rca),
                 card_->real_freq_khz);
        log_cccr_state("pre_essl");

        essl_sdio_config_t essl_config = {
            .card = card_,
            .recv_buffer_size = static_cast<int>(kSdioBufferSize),
        };
        err = essl_sdio_init_dev(&handle_, &essl_config);
        if (err != ESP_OK)
        {
            set_error("essl_sdio_init_failed");
            ESP_LOGW(kTag, "ESSL SDIO init failed: %s", esp_err_to_name(err));
            deinit_locked();
            return false;
        }

        err = essl_init(handle_, kHandshakeTimeoutMs);
        if (err != ESP_OK)
        {
            set_error(err == ESP_ERR_TIMEOUT ? "essl_init_fn_ready_timeout" : "essl_init_failed");
            ESP_LOGW(kTag,
                     "ESSL init failed stage=function_ready_or_cccr err=%s",
                     esp_err_to_name(err));
            deinit_locked();
            return false;
        }
        log_cccr_state("post_essl");

        ready_ = true;
        error_[0] = '\0';
        ESP_LOGI(kTag,
                 "SDIO transport ready slot=1 pins clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d",
                 pins_.clk,
                 pins_.cmd,
                 pins_.d0,
                 pins_.d1,
                 pins_.d2,
                 pins_.d3);
        return true;
    }

    bool send(const uint8_t* data, size_t len, uint32_t timeout_ms) override
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (!ready_ || handle_ == nullptr || data == nullptr || len == 0 ||
            len > tx_dma_scratch_.size())
        {
            set_error("sdio_send_invalid_state");
            return false;
        }

        std::memcpy(tx_dma_scratch_.data(), data, len);
        const esp_err_t err = essl_send_packet(handle_, tx_dma_scratch_.data(), len, timeout_ms);
        if (err != ESP_OK)
        {
            set_error("sdio_send_failed");
            ESP_LOGW(kTag, "SDIO send failed len=%u err=%s", static_cast<unsigned>(len), esp_err_to_name(err));
            return false;
        }
        return true;
    }

    bool recv(uint8_t* data, size_t max_len, size_t& out_len, uint32_t timeout_ms) override
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        out_len = 0;
        if (!ready_ || handle_ == nullptr || data == nullptr || max_len == 0)
        {
            set_error("sdio_recv_invalid_state");
            last_rx_err_ = ESP_ERR_INVALID_STATE;
            last_rx_size_ = 0;
            last_size_read_ = 0;
            return false;
        }

        esp_err_t err = ESP_OK;
        last_rx_size_ = 0;
        last_size_read_ = 0;
        uint32_t rx_size = 0;
        err = essl_get_rx_data_size(handle_, &rx_size, 0);
        last_rx_err_ = err;
        last_rx_size_ = rx_size;
        if (err != ESP_OK)
        {
            set_error("sdio_rx_size_failed");
            ESP_LOGW(kTag,
                     "SDIO rx size failed timeout_ms=%lu err=%s",
                     0UL,
                     esp_err_to_name(err));
            return false;
        }

        if (rx_size == 0)
        {
            err = essl_wait_int(handle_, timeout_ms);
            last_rx_err_ = err;
            if (err == ESP_ERR_TIMEOUT)
            {
                set_error("sdio_rx_empty");
                return false;
            }
            if (err != ESP_OK)
            {
                set_error("sdio_wait_int_failed");
                ESP_LOGW(kTag,
                         "SDIO wait interrupt failed timeout_ms=%lu err=%s",
                         static_cast<unsigned long>(timeout_ms),
                         esp_err_to_name(err));
                return false;
            }

            uint32_t intr_raw = 0;
            uint32_t intr_st = 0;
            err = essl_get_intr(handle_, &intr_raw, &intr_st, kShortIoTimeoutMs);
            last_rx_err_ = err;
            if (err != ESP_OK)
            {
                set_error("sdio_get_intr_failed");
                ESP_LOGW(kTag, "SDIO get intr failed err=%s", esp_err_to_name(err));
                return false;
            }

            if (intr_raw != 0)
            {
                const esp_err_t clear_err = essl_clear_intr(handle_, intr_raw, kShortIoTimeoutMs);
                if (clear_err != ESP_OK)
                {
                    set_error("sdio_clear_intr_failed");
                    last_rx_err_ = clear_err;
                    ESP_LOGW(kTag,
                             "SDIO clear intr failed raw=0x%08lX err=%s",
                             static_cast<unsigned long>(intr_raw),
                             esp_err_to_name(clear_err));
                    return false;
                }
            }

            if ((intr_raw & ESSL_SDIO_DEF_ESP32C6.new_packet_intr_mask) == 0)
            {
                set_error("sdio_rx_empty");
                last_rx_err_ = ESP_ERR_NOT_FOUND;
                return false;
            }

            err = essl_get_rx_data_size(handle_, &rx_size, kShortIoTimeoutMs);
            last_rx_err_ = err;
            last_rx_size_ = rx_size;
            if (err != ESP_OK)
            {
                set_error("sdio_rx_size_failed");
                ESP_LOGW(kTag,
                         "SDIO rx size failed timeout_ms=%lu err=%s",
                         static_cast<unsigned long>(kShortIoTimeoutMs),
                         esp_err_to_name(err));
                return false;
            }
        }

        if (rx_size == 0)
        {
            set_error("sdio_rx_empty");
            last_rx_err_ = ESP_ERR_NOT_FOUND;
            return false;
        }

        const size_t read_capacity = std::min(max_len, rx_dma_scratch_.size());
        size_t size_read = 0;
        err = essl_get_packet(handle_,
                              rx_dma_scratch_.data(),
                              read_capacity,
                              &size_read,
                              timeout_ms);
        last_rx_err_ = err;
        last_size_read_ = size_read;
        if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED)
        {
            set_error("sdio_recv_failed");
            ESP_LOGW(kTag,
                     "SDIO recv failed rx_size=%lu size_read=%u err=%s",
                     static_cast<unsigned long>(rx_size),
                     static_cast<unsigned>(size_read),
                     esp_err_to_name(err));
            return false;
        }
        if (size_read == 0 || size_read > read_capacity)
        {
            set_error("sdio_recv_invalid_length");
            last_rx_err_ = ESP_ERR_INVALID_SIZE;
            ESP_LOGW(kTag,
                     "SDIO recv invalid length rx_size=%lu size_read=%u capacity=%u",
                     static_cast<unsigned long>(rx_size),
                     static_cast<unsigned>(size_read),
                     static_cast<unsigned>(read_capacity));
            return false;
        }
        std::memcpy(data, rx_dma_scratch_.data(), size_read);
        out_len = size_read;
        ++rx_chunk_count_;
        if (rx_chunk_count_ <= 8 || (rx_chunk_count_ % 64) == 0)
        {
            ESP_LOGI(kTag,
                     "SDIO recv chunk count=%lu pending_before=%lu size_read=%u more=%u",
                     static_cast<unsigned long>(rx_chunk_count_),
                     static_cast<unsigned long>(rx_size),
                     static_cast<unsigned>(size_read),
                     err == ESP_ERR_NOT_FINISHED ? 1U : 0U);
        }
        return true;
    }

    void reset() override
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        deinit_locked();
    }

    const char* last_error() const override
    {
        return error_[0] != '\0' ? error_ : "sdio_no_error_detail";
    }

    uint32_t last_rx_size() const override
    {
        return last_rx_size_;
    }

    int last_rx_error() const override
    {
        return last_rx_err_;
    }

    size_t last_size_read() const override
    {
        return last_size_read_;
    }

  private:
    struct Pins
    {
        int clk = -1;
        int cmd = -1;
        int d0 = -1;
        int d1 = -1;
        int d2 = -1;
        int d3 = -1;
    };

    void set_error(const char* text)
    {
        std::snprintf(error_, sizeof(error_), "%s", text ? text : "sdio_error");
    }

    static void configure_pullup(int gpio)
    {
        if (gpio < 0)
        {
            return;
        }
        const gpio_num_t pin = static_cast<gpio_num_t>(gpio);
        (void)gpio_pullup_en(pin);
        (void)gpio_pulldown_dis(pin);
    }

    void apply_board_pins(sdmmc_slot_config_t& slot_config)
    {
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
        const auto& pins = ::boards::tab5::Tab5Board::profile().c6_sdio;
        pins_ = {pins.clk, pins.cmd, pins.d0, pins.d1, pins.d2, pins.d3};
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        const auto& pins = ::boards::t_display_p4::TDisplayP4Board::profile().c6_sdio;
        pins_ = {pins.clk, pins.cmd, pins.d0, pins.d1, pins.d2, pins.d3};
#endif
        slot_config.clk = static_cast<gpio_num_t>(pins_.clk);
        slot_config.cmd = static_cast<gpio_num_t>(pins_.cmd);
        slot_config.d0 = static_cast<gpio_num_t>(pins_.d0);
        slot_config.d1 = static_cast<gpio_num_t>(pins_.d1);
        slot_config.d2 = static_cast<gpio_num_t>(pins_.d2);
        slot_config.d3 = static_cast<gpio_num_t>(pins_.d3);
        configure_pullup(pins_.clk);
        configure_pullup(pins_.cmd);
        configure_pullup(pins_.d0);
        configure_pullup(pins_.d1);
        configure_pullup(pins_.d2);
        configure_pullup(pins_.d3);
    }

    void log_cccr_state(const char* stage) const
    {
        if (card_ == nullptr)
        {
            return;
        }

        uint8_t ioe = 0;
        uint8_t ior = 0;
        uint8_t ie = 0;
        const esp_err_t ioe_err = sdmmc_io_read_byte(card_, 0, SD_IO_CCCR_FN_ENABLE, &ioe);
        const esp_err_t ior_err = sdmmc_io_read_byte(card_, 0, SD_IO_CCCR_FN_READY, &ior);
        const esp_err_t ie_err = sdmmc_io_read_byte(card_, 0, SD_IO_CCCR_INT_ENABLE, &ie);

        if (ioe_err == ESP_OK && ior_err == ESP_OK && ie_err == ESP_OK)
        {
            ESP_LOGI(kTag,
                     "SDIO CCCR %s ioe=0x%02x ior=0x%02x int_ena=0x%02x",
                     stage ? stage : "probe",
                     ioe,
                     ior,
                     ie);
            return;
        }

        ESP_LOGW(kTag,
                 "SDIO CCCR %s probe failed ioe=%s ior=%s int_ena=%s",
                 stage ? stage : "probe",
                 esp_err_to_name(ioe_err),
                 esp_err_to_name(ior_err),
                 esp_err_to_name(ie_err));
    }

    bool prepare_board_companion()
    {
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        auto& board = ::boards::t_display_p4::TDisplayP4Board::instance();
        const auto& profile = ::boards::t_display_p4::TDisplayP4Board::profile();
        const auto& io = profile.io_expander;
        if (!board.expanderPinMode(io.c6_enable, true))
        {
            set_error("c6_enable_pin_mode_failed");
            return false;
        }
        if (!board.expanderWriteActive(io.c6_enable, true, profile.c6_enable_active_high))
        {
            set_error("c6_enable_failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!board.expanderWriteActive(io.c6_enable, false, profile.c6_enable_active_high))
        {
            set_error("c6_reset_assert_failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!board.expanderWriteActive(io.c6_enable, true, profile.c6_enable_active_high))
        {
            set_error("c6_reset_release_failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(1000));
        return true;
#elif defined(TRAIL_MATE_ESP_BOARD_TAB5)
        const auto& profile = ::boards::tab5::Tab5Board::profile();
        if (profile.c6_reset_gpio_requires_validation)
        {
            ESP_LOGW(kTag,
                     "Tab5 C6 reset GPIO%d requires validation; skipping reset pulse",
                     profile.c6_reset_gpio);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        return true;
#else
        return false;
#endif
    }

    void deinit_locked()
    {
        ready_ = false;
        if (handle_ != nullptr)
        {
            (void)essl_sdio_deinit_dev(handle_);
            handle_ = nullptr;
        }
        if (card_ != nullptr)
        {
            free(card_);
            card_ = nullptr;
        }
        if (slot_initialized_)
        {
            const esp_err_t err = sdmmc_host_runtime::release_slot(
                sdmmc_host_runtime::SlotOwner::C6Companion, SDMMC_HOST_SLOT_1);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            {
                ESP_LOGW(kTag, "SDIO slot release failed: %s", esp_err_to_name(err));
            }
            slot_initialized_ = false;
        }
    }

    essl_handle_t handle_ = nullptr;
    sdmmc_card_t* card_ = nullptr;
    std::mutex io_mutex_{};
    alignas(64) std::array<uint8_t, kSdioBufferSize> tx_dma_scratch_{};
    alignas(64) std::array<uint8_t, kSdioBufferSize> rx_dma_scratch_{};
    Pins pins_{};
    bool ready_ = false;
    bool slot_initialized_ = false;
    uint32_t last_rx_size_ = 0;
    int last_rx_err_ = ESP_OK;
    size_t last_size_read_ = 0;
    uint32_t rx_chunk_count_ = 0;
    char error_[64] = {};
};
#endif

class C6CompanionRuntime final : public WirelessCompanion, public WifiTcpTransport
{
  public:
    bool begin() override
    {
        if (status_.started)
        {
            return status_.present;
        }

        status_.started = true;
        status_.board_capable = board_has_c6_companion();
        status_.protocol_min = TM_C6_PROTO_MIN;
        status_.protocol_max = TM_C6_PROTO_MAX;
        status_.supported_features = 0;
        status_.enabled_features = 0;
        status_.config_seq = 0;
        status_.config_error = 0;
        status_.selected_mtu = 0;
        status_.ble_state = 0;
        status_.espnow_state = 0;
        status_.wifi_state = 0;
        status_.ble_uplink_count = 0;
        status_.ble_event_count = 0;
        status_.espnow_uplink_count = 0;
        status_.espnow_event_count = 0;
        status_.wifi_event_count = 0;
        status_.wifi_connected = false;
        status_.wifi_scanning = false;
        status_.wifi_error = TM_C6_OK;
        status_.wifi_ipv4_addr = 0;
        status_.wifi_scan_result_count = 0;
        status_.wifi_ssid[0] = '\0';
        reset_tcp_state(WifiTcpState::Disconnected, "not_connected");
        for (auto& result : status_.wifi_scan_results)
        {
            result = WifiScanResult{};
        }
        load_p4_settings();

        if (!status_.board_capable)
        {
            set_detail(status_, CompanionState::Unsupported, "target_has_no_c6_companion");
            return false;
        }

        status_.present = false;
        status_.state = CompanionState::TransportPending;
        status_.detail = "sdio_transport_probe_pending";
        status_.ping_nonce = 0xC6000001u;
        status_.ping_count = 1;
#if defined(ESP_PLATFORM)
        ESP_LOGW(kTag,
                 "C6 Phase 1 HostLink starting SDIO transport probe; "
                 "continuing product boot if C6 is unavailable");
        ESP_LOGI(kTag,
                 "HELLO request proto=%u..%u p4_fw=%lu requested_features=0x%08lx preferred_mtu=%u max_payload=%u seq=%u",
                 TM_C6_PROTO_MIN,
                 TM_C6_PROTO_MAX,
                 (unsigned long)TM_C6_P4_FIRMWARE_VERSION_UNKNOWN,
                 (unsigned long)requested_features_for(ble_config_, espnow_config_, wifi_config_),
                 TM_C6_MAX_PAYLOAD,
                 TM_C6_MAX_PAYLOAD,
                 kInitialSequence);
        ESP_LOGI(kTag,
                 "PING template nonce=0x%08lx seq=%u channel=%u",
                 (unsigned long)status_.ping_nonce,
                 static_cast<unsigned>(kInitialSequence + 1),
                 static_cast<unsigned>(TM_C6_CH_CONTROL));
#endif
        return try_handshake();
    }

    bool isPresent() const override
    {
        return status_.present;
    }

    uint32_t capabilities() const override
    {
        return status_.supported_features;
    }

    C6CompanionStatus status() const override
    {
        C6CompanionStatus copy = status_;
        copy.board_capable = board_has_c6_companion();
        if (!copy.started && copy.board_capable)
        {
            copy.state = CompanionState::NotStarted;
            copy.detail = "not_started";
        }
        return copy;
    }

    bool enterDownloadMode()
    {
        status_.started = true;
        status_.board_capable = board_has_c6_companion();
        status_.present = false;
        status_.supported_features = 0;
        status_.enabled_features = 0;
        status_.ble_state = 0;
        status_.espnow_state = 0;
        status_.wifi_state = 0;

        if (!status_.board_capable)
        {
            set_detail(status_, CompanionState::Unsupported, "target_has_no_c6_companion");
            return false;
        }

#if defined(ESP_PLATFORM)
        if (transport_)
        {
            transport_->reset();
            transport_.reset();
        }
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        const char* detail = "download_mode_failed";
        const bool ok = t_display_p4_enter_c6_download_mode(&detail);
        set_detail(status_, ok ? CompanionState::Missing : CompanionState::Error, detail);
        ESP_LOGI(kTag,
                 "C6 enter download mode result ok=%u detail=%s",
                 ok ? 1u : 0u,
                 detail ? detail : "unknown");
        return ok;
#else
        set_detail(status_, CompanionState::Unsupported, "c6_download_mode_unsupported_board");
        return false;
#endif
#else
        set_detail(status_, CompanionState::Unsupported, "c6_download_mode_unsupported_platform");
        return false;
#endif
    }

    bool configureBle(const BleCompanionConfig& config) override
    {
        ble_config_ = config;
        return send_config_if_ready();
    }

    bool configureEspNow(const EspNowCompanionConfig& config) override
    {
        espnow_config_ = config;
        return send_config_if_ready();
    }

    bool configureWifi(const WifiCompanionConfig& config) override
    {
        wifi_config_ = config;
        return send_config_if_ready();
    }

    bool sendBleDownlink(BleProfile profile, const uint8_t* data, size_t len) override
    {
        if (!status_.present || profile == BleProfile::None ||
            (data == nullptr && len != 0) ||
            len > TM_C6_MAX_PAYLOAD - sizeof(tm_c6_ble_packet_header_t))
        {
            return false;
        }

        std::array<uint8_t, TM_C6_MAX_PAYLOAD> payload{};
        tm_c6_ble_packet_header_t header{};
        header.profile = static_cast<uint8_t>(profile);
        header.connection_id = 0;
        header.payload_len = static_cast<uint16_t>(len);
        std::memcpy(payload.data(), &header, sizeof(header));
        if (len > 0)
        {
            std::memcpy(payload.data() + sizeof(header), data, len);
        }

        return send_frame(TM_C6_FRAME_BLE_DOWNLINK,
                          channel_for_ble_profile(profile),
                          TM_C6_FLAG_ACK_REQUIRED,
                          0,
                          payload.data(),
                          sizeof(header) + len);
    }

    bool sendEspNow(const EspNowPacket& packet) override
    {
        if (!status_.present || packet.payload_len > TM_C6_ESPNOW_PAYLOAD_MAX)
        {
            return false;
        }

        tm_c6_espnow_packet_t wire{};
        std::memcpy(wire.peer_mac, packet.peer_mac, sizeof(wire.peer_mac));
        wire.rssi_valid = bool_byte(packet.rssi_valid);
        wire.rssi = packet.rssi;
        wire.channel = packet.channel;
        wire.payload_len = packet.payload_len;
        if (packet.payload_len > 0)
        {
            std::memcpy(wire.payload, packet.payload, packet.payload_len);
        }

        return send_frame(TM_C6_FRAME_ESPNOW_DOWNLINK,
                          TM_C6_CH_ESPNOW_TEAM,
                          TM_C6_FLAG_ACK_REQUIRED,
                          0,
                          reinterpret_cast<const uint8_t*>(&wire),
                          sizeof(wire));
    }

    bool sendWifiControl(const WifiControl& control) override
    {
        if (!status_.present)
        {
            return false;
        }

        tm_c6_wifi_control_t wire{};
        wire.command = static_cast<uint8_t>(control.command);
        wire.flags = control.flags;
        copy_text(wire.ssid, control.ssid);
        copy_text(wire.password, control.password);
        wire.channel = control.channel;

        if (control.command == WifiCommand::Scan)
        {
            status_.wifi_scanning = true;
        }

        const bool sent = send_retryable_control_frame(
            "wifi_control",
            TM_C6_FRAME_WIFI_CONTROL,
            TM_C6_CH_WIFI_MGMT,
            TM_C6_FLAG_ACK_REQUIRED,
            0,
            reinterpret_cast<const uint8_t*>(&wire),
            sizeof(wire));
        if (!sent && control.command == WifiCommand::Scan)
        {
            status_.wifi_scanning = false;
        }
        return sent;
    }

    bool openTcp(const char* host, uint16_t port) override
    {
        if (!host || host[0] == '\0' || std::strlen(host) >= TM_C6_WIFI_TCP_HOST_LEN || port == 0)
        {
            set_tcp_state(WifiTcpState::Error, TM_C6_ERROR_INTERNAL, "invalid_endpoint");
            return false;
        }
        if (!status_.started)
        {
            (void)begin();
        }
        if (!status_.present || !status_.wifi_connected ||
            (status_.supported_features & TM_C6_FEATURE_WIFI_TCP_PROXY) == 0)
        {
            set_tcp_state(WifiTcpState::Disconnected, TM_C6_ERROR_NOT_CONNECTED, "wifi_not_ready");
            return false;
        }

        reset_tcp_state(WifiTcpState::Disconnected, "opening_reset");
        if (!prepare_tcp_rx())
        {
            set_tcp_state(WifiTcpState::Error, TM_C6_ERROR_INTERNAL, "rx_memory_unavailable");
            return false;
        }
        tcp_status_.connection_id = next_tcp_connection_id_++;
        if (next_tcp_connection_id_ == 0)
        {
            next_tcp_connection_id_ = 1;
        }

        tm_c6_wifi_tcp_header_t header{};
        header.operation = TM_C6_WIFI_TCP_OPEN;
        header.connection_id = tcp_status_.connection_id;
        header.port = port;
        copy_text(header.host, host);
        if (!send_wifi_tcp_command(header, nullptr, 0))
        {
            set_tcp_state(WifiTcpState::Error, TM_C6_ERROR_INTERNAL, "open_send_failed");
            return false;
        }
        set_tcp_state(WifiTcpState::Opening, TM_C6_OK, "opening");
        return true;
    }

    bool writeTcp(const uint8_t* data, size_t len) override
    {
        if (tcp_status_.state != WifiTcpState::Connected || (data == nullptr && len != 0) ||
            len == 0)
        {
            return false;
        }

        size_t offset = 0;
        while (offset < len)
        {
            const size_t chunk = std::min<size_t>(TM_C6_WIFI_TCP_PAYLOAD_MAX, len - offset);
            tm_c6_wifi_tcp_header_t header{};
            header.operation = TM_C6_WIFI_TCP_WRITE;
            header.connection_id = tcp_status_.connection_id;
            header.payload_len = static_cast<uint16_t>(chunk);
            if (!send_wifi_tcp_command(header, data + offset, chunk))
            {
                set_tcp_state(WifiTcpState::Error, TM_C6_ERROR_INTERNAL, "write_send_failed");
                return false;
            }
            offset += chunk;
        }
        tcp_status_.transmitted_bytes += static_cast<uint32_t>(len);
        return true;
    }

    size_t readTcp(uint8_t* out, size_t max_len) override
    {
        if (!out || max_len == 0)
        {
            return 0;
        }
        poll();

        std::lock_guard<std::mutex> lock(tcp_rx_mutex_);
        const size_t read_len = std::min(max_len, tcp_rx_size_);
        for (size_t i = 0; i < read_len; ++i)
        {
            out[i] = (*tcp_rx_buffer_)[tcp_rx_tail_];
            tcp_rx_tail_ = (tcp_rx_tail_ + 1) % kWifiTcpRxBufferSize;
        }
        tcp_rx_size_ -= read_len;
        tcp_status_.buffered_bytes = tcp_rx_size_;
        // A remote EOF may follow the final payload in the same poll. Keep
        // those bytes readable, then return the session's PSRAM after draining.
        if (tcp_rx_size_ == 0 &&
            (tcp_status_.state == WifiTcpState::Disconnected || tcp_status_.state == WifiTcpState::Error))
        {
            tcp_rx_buffer_.reset();
        }
        return read_len;
    }

    void closeTcp() override
    {
        if (tcp_status_.state == WifiTcpState::Disconnected ||
            tcp_status_.state == WifiTcpState::Unsupported)
        {
            reset_tcp_rx();
            return;
        }

        tm_c6_wifi_tcp_header_t header{};
        header.operation = TM_C6_WIFI_TCP_CLOSE;
        header.connection_id = tcp_status_.connection_id;
        if (status_.present && send_wifi_tcp_command(header, nullptr, 0))
        {
            set_tcp_state(WifiTcpState::Closing, TM_C6_OK, "closing");
            return;
        }
        reset_tcp_state(WifiTcpState::Disconnected, "closed_locally");
    }

    WifiTcpStatus tcpStatus() const override
    {
        return tcp_status_;
    }

    void poll() override
    {
        if (!status_.started || !status_.present)
        {
            return;
        }
#if defined(ESP_PLATFORM)
        if (!transport_)
        {
            return;
        }
        std::unique_lock<std::recursive_mutex> receive_owner(receive_owner_mutex_, std::try_to_lock);
        if (!receive_owner.owns_lock())
        {
            return;
        }
        for (size_t i = 0; i < 8; ++i)
        {
            hostlink::c6::Frame frame{};
            if (!receive_frame(frame, 0, false))
            {
                break;
            }
            handle_async_frame(frame);
        }
#endif
    }

  private:
    uint16_t next_seq()
    {
        const uint16_t current = next_seq_++;
        if (next_seq_ == 0)
        {
            next_seq_ = 1;
        }
        return current;
    }

    bool encode(uint8_t frame_type,
                uint8_t channel,
                uint16_t flags,
                uint16_t ack,
                const uint8_t* payload,
                size_t payload_len,
                std::vector<uint8_t>& out)
    {
        hostlink::c6::EncodeRequest request{};
        request.frame_type = frame_type;
        request.channel = channel;
        request.flags = flags;
        request.seq = next_seq();
        request.ack = ack;
        request.payload = payload;
        request.payload_len = payload_len;
        return hostlink::c6::encode_frame(request, out, TM_C6_MAX_PAYLOAD);
    }

    void consume_rx_stream(size_t count)
    {
        if (count >= frame_rx_stream_size_)
        {
            frame_rx_stream_size_ = 0;
            return;
        }
        std::memmove(frame_rx_stream_.data(),
                     frame_rx_stream_.data() + count,
                     frame_rx_stream_size_ - count);
        frame_rx_stream_size_ -= count;
    }

    size_t resync_rx_stream()
    {
        constexpr std::array<uint8_t, 4> magic = {
            static_cast<uint8_t>(TM_C6_MAGIC & 0xFFu),
            static_cast<uint8_t>((TM_C6_MAGIC >> 8) & 0xFFu),
            static_cast<uint8_t>((TM_C6_MAGIC >> 16) & 0xFFu),
            static_cast<uint8_t>((TM_C6_MAGIC >> 24) & 0xFFu),
        };

        for (size_t offset = 1; offset + magic.size() <= frame_rx_stream_size_; ++offset)
        {
            if (std::equal(magic.begin(), magic.end(), frame_rx_stream_.begin() + offset))
            {
                consume_rx_stream(offset);
                return offset;
            }
        }

        size_t keep = 0;
        const size_t max_prefix = std::min(magic.size() - 1, frame_rx_stream_size_);
        for (size_t candidate = max_prefix; candidate > 0; --candidate)
        {
            if (std::equal(magic.begin(),
                           magic.begin() + static_cast<std::ptrdiff_t>(candidate),
                           frame_rx_stream_.begin() +
                               static_cast<std::ptrdiff_t>(frame_rx_stream_size_ - candidate)))
            {
                keep = candidate;
                break;
            }
        }

        const size_t dropped = frame_rx_stream_size_ - keep;
        if (keep > 0)
        {
            std::memmove(frame_rx_stream_.data(),
                         frame_rx_stream_.data() + dropped,
                         keep);
        }
        frame_rx_stream_size_ = keep;
        return dropped;
    }

    bool receive_frame(hostlink::c6::Frame& frame, uint32_t timeout_ms, bool update_status_on_empty = true)
    {
#if defined(ESP_PLATFORM)
        if (!transport_)
        {
            return false;
        }
        const TickType_t start_ticks = xTaskGetTickCount();
        const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
        uint32_t attempts = 0;
        while (true)
        {
            if (frame_rx_stream_size_ > 0)
            {
                const auto decoded = hostlink::c6::decode_frame(
                    frame_rx_stream_.data(), frame_rx_stream_size_, TM_C6_MAX_PAYLOAD);
                if (decoded.status == hostlink::c6::DecodeStatus::Ok)
                {
                    frame = decoded.frame;
                    consume_rx_stream(decoded.bytes_consumed);
                    if (attempts > 1)
                    {
                        const uint32_t elapsed_ms = static_cast<uint32_t>(
                            (xTaskGetTickCount() - start_ticks) * portTICK_PERIOD_MS);
                        ESP_LOGI(kTag,
                                 "C6 recv frame after retry attempts=%lu elapsed_ms=%lu buffered=%u",
                                 static_cast<unsigned long>(attempts),
                                 static_cast<unsigned long>(elapsed_ms),
                                 static_cast<unsigned>(frame_rx_stream_size_));
                    }
                    return true;
                }
                if (decoded.status != hostlink::c6::DecodeStatus::Incomplete)
                {
                    const size_t dropped = resync_rx_stream();
                    ++frame_rx_resync_count_;
                    if (frame_rx_resync_count_ <= 8 || (frame_rx_resync_count_ % 32) == 0)
                    {
                        ESP_LOGW(kTag,
                                 "C6 RX stream resync count=%lu status=%s dropped=%u buffered=%u",
                                 static_cast<unsigned long>(frame_rx_resync_count_),
                                 hostlink::c6::decode_status_name(decoded.status),
                                 static_cast<unsigned>(dropped),
                                 static_cast<unsigned>(frame_rx_stream_size_));
                    }
                    continue;
                }
            }

            if (frame_rx_stream_size_ >= frame_rx_stream_.size())
            {
                frame_rx_stream_size_ = 0;
                set_detail(status_, CompanionState::Error, "c6_rx_stream_overflow");
                ESP_LOGW(kTag, "C6 RX stream overflow capacity=%u", static_cast<unsigned>(frame_rx_stream_.size()));
                return false;
            }

            ++attempts;
            uint32_t poll_timeout_ms = 0;
            if (timeout_ms > 0)
            {
                const uint32_t elapsed_ms = static_cast<uint32_t>(
                    (xTaskGetTickCount() - start_ticks) * portTICK_PERIOD_MS);
                if (elapsed_ms >= timeout_ms)
                {
                    if (update_status_on_empty)
                    {
                        set_detail(status_, CompanionState::Missing, "sdio_rx_empty");
                    }
                    return false;
                }
                poll_timeout_ms = std::min(timeout_ms - elapsed_ms, kReceivePollTimeoutMs);
            }

            size_t rx_len = 0;
            const size_t stream_capacity = frame_rx_stream_.size() - frame_rx_stream_size_;
            if (transport_->recv(frame_rx_stream_.data() + frame_rx_stream_size_,
                                 stream_capacity,
                                 rx_len,
                                 poll_timeout_ms))
            {
                frame_rx_stream_size_ += rx_len;
                continue;
            }

            const char* detail = transport_->last_error();
            const int rx_error = transport_->last_rx_error();
            const bool may_retry_empty =
                (rx_error == ESP_ERR_TIMEOUT || rx_error == ESP_ERR_NOT_FOUND) && timeout_ms > 0;
            const TickType_t elapsed_ticks = xTaskGetTickCount() - start_ticks;
            if (!may_retry_empty || elapsed_ticks >= timeout_ticks)
            {
                if (update_status_on_empty)
                {
                    set_detail(status_, CompanionState::Missing, detail);
                }
                return false;
            }

            vTaskDelay(pdMS_TO_TICKS(kReceiveRetryDelayMs));
        }
#else
        (void)frame;
        (void)timeout_ms;
        return false;
#endif
    }

    bool send_frame(uint8_t frame_type,
                    uint8_t channel,
                    uint16_t flags,
                    uint16_t ack,
                    const uint8_t* payload,
                    size_t payload_len)
    {
#if defined(ESP_PLATFORM)
        std::lock_guard<std::mutex> lock(send_mutex_);
        return send_frame_locked(frame_type, channel, flags, ack, payload, payload_len);
#else
        (void)frame_type;
        (void)channel;
        (void)flags;
        (void)ack;
        (void)payload;
        (void)payload_len;
        return false;
#endif
    }

    bool send_frame_locked(uint8_t frame_type,
                           uint8_t channel,
                           uint16_t flags,
                           uint16_t ack,
                           const uint8_t* payload,
                           size_t payload_len)
    {
#if defined(ESP_PLATFORM)
        std::vector<uint8_t> out;
        if (!encode(frame_type, channel, flags, ack, payload, payload_len, out))
        {
            set_detail(status_, CompanionState::Error, "c6_encode_failed");
            return false;
        }
        if (!transport_ || !transport_->send(out.data(), out.size(), kShortIoTimeoutMs))
        {
            set_detail(status_, CompanionState::Missing, transport_ ? transport_->last_error() : "transport_missing");
            return false;
        }
        return true;
#else
        (void)frame_type;
        (void)channel;
        (void)flags;
        (void)ack;
        (void)payload;
        (void)payload_len;
        return false;
#endif
    }

    bool send_retryable_control_frame(const char* operation,
                                      uint8_t frame_type,
                                      uint8_t channel,
                                      uint16_t flags,
                                      uint16_t ack,
                                      const uint8_t* payload,
                                      size_t payload_len)
    {
        const bool retry_allowed = status_.started && status_.present && transport_ != nullptr;
        if (send_frame(frame_type, channel, flags, ack, payload, payload_len))
        {
            return true;
        }
        if (!retry_allowed)
        {
            return false;
        }

#if defined(ESP_PLATFORM)
        ESP_LOGW(kTag,
                 "C6 %s send failed detail=%s; draining pending frames before one retry",
                 operation ? operation : "control",
                 status_.detail ? status_.detail : "unknown");

        // Long LoRa transmissions can temporarily starve the cooperative C6
        // pump. Restore the known-present state long enough to drain queued
        // async frames, then retry only this idempotent control operation.
        // TCP data deliberately stays on the non-retrying send_frame() path.
        status_.present = true;
        status_.state = CompanionState::Present;
        status_.detail = "control_send_retry";
        poll();
        vTaskDelay(pdMS_TO_TICKS(kReceiveRetryDelayMs));
        poll();

        const bool sent = send_frame(frame_type,
                                     channel,
                                     flags,
                                     ack,
                                     payload,
                                     payload_len);
        if (sent)
        {
            ESP_LOGI(kTag,
                     "C6 %s send recovered after draining pending frames",
                     operation ? operation : "control");
        }
        return sent;
#else
        (void)operation;
        return false;
#endif
    }

    bool send_wifi_tcp_command(const tm_c6_wifi_tcp_header_t& header,
                               const uint8_t* data,
                               size_t len)
    {
        if (len > TM_C6_WIFI_TCP_PAYLOAD_MAX || (data == nullptr && len != 0))
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(send_mutex_);
        std::memcpy(wifi_tcp_tx_buffer_.data(), &header, sizeof(header));
        if (len > 0)
        {
            std::memcpy(wifi_tcp_tx_buffer_.data() + sizeof(header), data, len);
        }
        return send_frame_locked(TM_C6_FRAME_WIFI_DATA,
                                 TM_C6_CH_WIFI_DATA,
                                 TM_C6_FLAG_ACK_REQUIRED,
                                 0,
                                 wifi_tcp_tx_buffer_.data(),
                                 sizeof(header) + len);
    }

    bool prepare_tcp_rx()
    {
        std::lock_guard<std::mutex> lock(tcp_rx_mutex_);
        if (!tcp_rx_buffer_)
        {
            tcp_rx_buffer_.reset(::platform::memory::createPsram<TcpRxBuffer>());
        }
        return tcp_rx_buffer_ != nullptr;
    }

    void reset_tcp_rx()
    {
        std::lock_guard<std::mutex> lock(tcp_rx_mutex_);
        tcp_rx_head_ = 0;
        tcp_rx_tail_ = 0;
        tcp_rx_size_ = 0;
        tcp_status_.buffered_bytes = 0;
        tcp_rx_buffer_.reset();
    }

    void set_tcp_state(WifiTcpState state, uint16_t error_code, const char* detail)
    {
        std::lock_guard<std::mutex> lock(tcp_rx_mutex_);
        tcp_status_.state = state;
        tcp_status_.error_code = error_code;
        tcp_status_.detail = detail;
        if (tcp_rx_size_ == 0 && (state == WifiTcpState::Disconnected || state == WifiTcpState::Error))
        {
            tcp_rx_buffer_.reset();
        }
    }

    void reset_tcp_state(WifiTcpState state, const char* detail)
    {
        const uint8_t connection_id = tcp_status_.connection_id;
        tcp_status_ = WifiTcpStatus{};
        tcp_status_.state = state;
        tcp_status_.connection_id = connection_id;
        tcp_status_.detail = detail;
        reset_tcp_rx();
    }

    bool append_tcp_rx(const uint8_t* data, size_t len)
    {
        std::lock_guard<std::mutex> lock(tcp_rx_mutex_);
        if ((!data && len != 0) || !tcp_rx_buffer_ || len > kWifiTcpRxBufferSize - tcp_rx_size_)
        {
            return false;
        }
        for (size_t i = 0; i < len; ++i)
        {
            (*tcp_rx_buffer_)[tcp_rx_head_] = data[i];
            tcp_rx_head_ = (tcp_rx_head_ + 1) % kWifiTcpRxBufferSize;
        }
        tcp_rx_size_ += len;
        tcp_status_.buffered_bytes = tcp_rx_size_;
        tcp_status_.received_bytes += static_cast<uint32_t>(len);
        return true;
    }

    bool send_hello()
    {
        uint8_t payload[sizeof(tm_c6_hello_t)] = {};
        const uint32_t requested_features = requested_features_for(ble_config_, espnow_config_, wifi_config_);
        write_le16(payload, TM_C6_PROTO_MIN);
        write_le16(payload + 2, TM_C6_PROTO_MAX);
        write_le32(payload + 4, TM_C6_P4_FIRMWARE_VERSION_UNKNOWN);
        write_le32(payload + 8, requested_features);
        write_le16(payload + 12, TM_C6_MAX_PAYLOAD);
        write_le16(payload + 14, TM_C6_MAX_PAYLOAD);
        return send_frame(TM_C6_FRAME_HELLO,
                          TM_C6_CH_CONTROL,
                          TM_C6_FLAG_ACK_REQUIRED,
                          0,
                          payload,
                          sizeof(payload));
    }

    bool handle_hello_ack(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type != TM_C6_FRAME_HELLO_ACK ||
            frame.channel != TM_C6_CH_CONTROL ||
            frame.payload.size() != sizeof(tm_c6_hello_ack_t))
        {
            set_detail(status_, CompanionState::Error, "unexpected_hello_ack");
            return false;
        }

        const uint8_t* payload = frame.payload.data();
        const uint16_t selected_proto = read_le16(payload);
        if (selected_proto < TM_C6_PROTO_MIN || selected_proto > TM_C6_PROTO_MAX)
        {
            set_detail(status_, CompanionState::Error, "protocol_mismatch");
            return false;
        }

        status_.selected_protocol = selected_proto;
        status_.firmware_version = read_le32(payload + 2);
        status_.supported_features = read_le32(payload + 6);
        status_.selected_mtu = read_le16(payload + 10);
        status_.free_heap = read_le32(payload + 14);
        return true;
    }

    bool send_ping()
    {
        status_.ping_nonce = 0xC6000001u + status_.ping_count;
        uint8_t payload[sizeof(tm_c6_ping_t)] = {};
        write_le32(payload, status_.ping_nonce);
#if defined(ESP_PLATFORM)
        write_le32(payload + 4, static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS));
#else
        write_le32(payload + 4, 0);
#endif
        ++status_.ping_count;
        return send_frame(TM_C6_FRAME_PING,
                          TM_C6_CH_CONTROL,
                          TM_C6_FLAG_ACK_REQUIRED,
                          0,
                          payload,
                          sizeof(payload));
    }

    bool handle_pong(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type != TM_C6_FRAME_PONG ||
            frame.channel != TM_C6_CH_CONTROL ||
            frame.payload.size() != sizeof(tm_c6_pong_t))
        {
            set_detail(status_, CompanionState::Error, "unexpected_pong");
            return false;
        }

        const uint8_t* payload = frame.payload.data();
        const uint32_t nonce = read_le32(payload);
        if (nonce != status_.ping_nonce)
        {
            set_detail(status_, CompanionState::Error, "pong_nonce_mismatch");
            return false;
        }

        ++status_.pong_count;
        status_.free_heap = read_le32(payload + 8);
        return true;
    }

    void load_p4_settings()
    {
        ble_config_ = load_ble_config_from_p4_settings();
        espnow_config_ = load_espnow_config_from_p4_settings();
        wifi_config_ = load_wifi_config_from_p4_settings();
    }

    tm_c6_companion_config_t build_config(uint32_t config_seq) const
    {
        tm_c6_companion_config_t config{};
        config.config_seq = config_seq;
        config.requested_features = requested_features_for(ble_config_, espnow_config_, wifi_config_);

        config.ble.ble_enabled = bool_byte(ble_config_.enabled);
        config.ble.meshtastic_enabled = bool_byte(ble_config_.meshtastic_enabled);
        config.ble.meshcore_enabled = bool_byte(ble_config_.meshcore_enabled);
        config.ble.trailmate_enabled = bool_byte(ble_config_.trailmate_enabled);
        config.ble.pairing_mode = static_cast<uint8_t>(ble_config_.pairing_mode);
        config.ble.fixed_pin_enabled = bool_byte(ble_config_.fixed_pin_enabled);
        copy_text(config.ble.fixed_pin, ble_config_.fixed_pin);
        copy_text(config.ble.device_name, ble_config_.device_name);
        config.ble.preferred_mtu = ble_config_.preferred_mtu;

        config.espnow.espnow_enabled = bool_byte(espnow_config_.enabled);
        config.espnow.team_discovery_enabled = bool_byte(espnow_config_.team_discovery_enabled);
        config.espnow.channel = espnow_config_.channel;
        config.espnow.beacon_interval_ms = espnow_config_.beacon_interval_ms;
        std::memcpy(config.espnow.broadcast_mac,
                    espnow_config_.broadcast_mac,
                    sizeof(config.espnow.broadcast_mac));

        config.wifi.wifi_enabled = bool_byte(wifi_config_.enabled);
        config.wifi.sta_enabled = bool_byte(wifi_config_.sta_enabled);
        config.wifi.ap_enabled = bool_byte(wifi_config_.ap_enabled);
        config.wifi.persist_credentials = bool_byte(wifi_config_.persist_credentials);
        copy_text(config.wifi.sta_ssid, wifi_config_.sta_ssid);
        copy_text(config.wifi.sta_password, wifi_config_.sta_password);
        copy_text(config.wifi.ap_ssid, wifi_config_.ap_ssid);
        copy_text(config.wifi.ap_password, wifi_config_.ap_password);
        config.wifi.sta_channel = wifi_config_.sta_channel;
        config.wifi.ap_channel = wifi_config_.ap_channel;
        return config;
    }

    bool send_config_set()
    {
        const uint32_t config_seq = next_config_seq_++;
        if (next_config_seq_ == 0)
        {
            next_config_seq_ = kDefaultConfigSequence;
        }
        const tm_c6_companion_config_t config = build_config(config_seq);
        status_.config_seq = config.config_seq;
        return send_retryable_control_frame(
            "config_set",
            TM_C6_FRAME_CONFIG_SET,
            TM_C6_CH_CONTROL,
            TM_C6_FLAG_ACK_REQUIRED,
            0,
            reinterpret_cast<const uint8_t*>(&config),
            sizeof(config));
    }

    bool send_config_if_ready()
    {
        std::lock_guard<std::recursive_mutex> receive_owner(receive_owner_mutex_);
        if (!status_.started || !status_.present)
        {
            return false;
        }
        if (!send_config_set())
        {
            return false;
        }
        hostlink::c6::Frame frame{};
        return receive_expected_frame(frame, TM_C6_FRAME_CONFIG_REPORT, "CONFIG_REPORT", kHandshakeTimeoutMs) &&
               handle_config_report(frame);
    }

    bool handle_config_report(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type != TM_C6_FRAME_CONFIG_REPORT ||
            frame.channel != TM_C6_CH_CONTROL ||
            frame.payload.size() != sizeof(tm_c6_config_report_t))
        {
            set_detail(status_, CompanionState::Error, "unexpected_config_report");
            return false;
        }

        tm_c6_config_report_t report{};
        std::memcpy(&report, frame.payload.data(), sizeof(report));
        status_.config_seq = report.config_seq;
        status_.supported_features = report.supported_features;
        status_.enabled_features = report.enabled_features;
        status_.free_heap = report.c6_free_heap;
        status_.config_error = report.error_code;
        status_.selected_mtu = report.selected_mtu;
        status_.ble_state = report.ble_state;
        status_.espnow_state = report.espnow_state;
        status_.wifi_state = report.wifi_state;

        if (report.error_code != TM_C6_OK)
        {
            set_detail(status_, CompanionState::Error, "config_report_error");
            return false;
        }
        return true;
    }

    void handle_async_frame(const hostlink::c6::Frame& frame)
    {
        if (frame.frame_type == TM_C6_FRAME_CONFIG_REPORT)
        {
            (void)handle_config_report(frame);
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_DIAG_REPORT &&
            frame.payload.size() == sizeof(tm_c6_diag_report_t))
        {
            tm_c6_diag_report_t report{};
            std::memcpy(&report, frame.payload.data(), sizeof(report));
            status_.free_heap = report.free_heap;
            status_.enabled_features = report.enabled_features;
            status_.config_error = report.last_error_code;
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_ERROR &&
            frame.payload.size() == sizeof(tm_c6_error_t))
        {
            const auto* error = reinterpret_cast<const tm_c6_error_t*>(frame.payload.data());
            if (error->related_channel == TM_C6_CH_WIFI_DATA)
            {
                set_tcp_state(WifiTcpState::Error, error->error_code, "c6_wifi_tcp_error");
                return;
            }
            status_.config_error = error->error_code;
            set_detail(status_, CompanionState::Error, "c6_error_frame");
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_BLE_UPLINK &&
            frame.payload.size() >= sizeof(tm_c6_ble_packet_header_t))
        {
            tm_c6_ble_packet_header_t header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            if (header.payload_len == frame.payload.size() - sizeof(header))
            {
                ++status_.ble_uplink_count;
#if defined(ESP_PLATFORM)
                ESP_LOGI(kTag,
                         "C6 BLE uplink profile=%u channel=%u len=%u unsupported",
                         static_cast<unsigned>(header.profile),
                         static_cast<unsigned>(frame.channel),
                         static_cast<unsigned>(header.payload_len));
#endif
            }
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_BLE_EVENT &&
            frame.payload.size() == sizeof(tm_c6_ble_event_t))
        {
            ++status_.ble_event_count;
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_ESPNOW_UPLINK &&
            frame.payload.size() == sizeof(tm_c6_espnow_packet_t))
        {
            ++status_.espnow_uplink_count;
#if defined(ESP_PLATFORM)
            const auto* packet = reinterpret_cast<const tm_c6_espnow_packet_t*>(frame.payload.data());
            ESP_LOGI(kTag,
                     "C6 ESP-NOW uplink channel=%u len=%u rssi_valid=%u pending_team_router",
                     static_cast<unsigned>(packet->channel),
                     static_cast<unsigned>(packet->payload_len),
                     static_cast<unsigned>(packet->rssi_valid));
#endif
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_ESPNOW_EVENT &&
            frame.payload.size() == sizeof(tm_c6_espnow_event_t))
        {
            ++status_.espnow_event_count;
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_WIFI_TIME_SYNC &&
            frame.payload.size() == sizeof(tm_c6_wifi_time_sync_t))
        {
            tm_c6_wifi_time_sync_t event{};
            std::memcpy(&event, frame.payload.data(), sizeof(event));
            apply_c6_wifi_time_sync(event);
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_WIFI_EVENT &&
            frame.payload.size() == sizeof(tm_c6_wifi_event_t))
        {
            ++status_.wifi_event_count;
            tm_c6_wifi_event_t event{};
            std::memcpy(&event, frame.payload.data(), sizeof(event));
            status_.wifi_error = event.error_code;
            copy_text(status_.wifi_ssid, event.ssid);
            status_.wifi_ipv4_addr = event.ipv4_addr;
#if defined(ESP_PLATFORM)
            ESP_LOGI(kTag,
                     "C6 Wi-Fi event kind=%u error=%u ssid=%s ipv4=0x%08lx results=%u count=%lu",
                     static_cast<unsigned>(event.event_kind),
                     static_cast<unsigned>(event.error_code),
                     event.ssid,
                     static_cast<unsigned long>(event.ipv4_addr),
                     static_cast<unsigned>(event.result_count),
                     static_cast<unsigned long>(status_.wifi_event_count));
#endif
            switch (event.event_kind)
            {
            case TM_C6_WIFI_EVENT_SCAN_DONE:
                status_.wifi_scanning = false;
                status_.wifi_scan_result_count = event.result_count <= 6 ? event.result_count : 6;
                for (uint8_t i = 0; i < status_.wifi_scan_result_count; ++i)
                {
                    copy_text(status_.wifi_scan_results[i].ssid, event.results[i].ssid);
                    status_.wifi_scan_results[i].rssi = event.results[i].rssi;
                    status_.wifi_scan_results[i].channel = event.results[i].channel;
                    status_.wifi_scan_results[i].authmode = event.results[i].authmode;
                }
                break;
            case TM_C6_WIFI_EVENT_STA_CONNECTED:
                status_.wifi_connected = false;
                status_.wifi_scanning = false;
                break;
            case TM_C6_WIFI_EVENT_STA_GOT_IP:
                status_.wifi_connected = event.ipv4_addr != 0;
                status_.wifi_scanning = false;
                break;
            case TM_C6_WIFI_EVENT_STA_DISCONNECTED:
            case TM_C6_WIFI_EVENT_STOPPED:
                status_.wifi_connected = false;
                status_.wifi_scanning = false;
                reset_tcp_state(WifiTcpState::Disconnected, "wifi_disconnected");
                break;
            case TM_C6_WIFI_EVENT_ERROR:
                status_.wifi_scanning = false;
                break;
            default:
                break;
            }
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_WIFI_DATA &&
            frame.channel == TM_C6_CH_WIFI_DATA &&
            frame.payload.size() >= sizeof(tm_c6_wifi_tcp_header_t))
        {
            tm_c6_wifi_tcp_header_t header{};
            std::memcpy(&header, frame.payload.data(), sizeof(header));
            const size_t data_len = frame.payload.size() - sizeof(header);
            if (header.payload_len != data_len ||
                header.connection_id != tcp_status_.connection_id)
            {
                return;
            }

            switch (header.operation)
            {
            case TM_C6_WIFI_TCP_OPENED:
                set_tcp_state(WifiTcpState::Connected, TM_C6_OK, "connected");
                break;
            case TM_C6_WIFI_TCP_DATA:
                if (!append_tcp_rx(frame.payload.data() + sizeof(header), data_len))
                {
                    set_tcp_state(WifiTcpState::Error, TM_C6_ERROR_QUEUE_FULL, "rx_overflow");
                    tm_c6_wifi_tcp_header_t close_header{};
                    close_header.operation = TM_C6_WIFI_TCP_CLOSE;
                    close_header.connection_id = tcp_status_.connection_id;
                    (void)send_wifi_tcp_command(close_header, nullptr, 0);
                }
                break;
            case TM_C6_WIFI_TCP_CLOSED:
                set_tcp_state(WifiTcpState::Disconnected, TM_C6_OK, "remote_closed");
                break;
            case TM_C6_WIFI_TCP_ERROR:
                set_tcp_state(WifiTcpState::Error, header.error_code, "remote_error");
                break;
            default:
                break;
            }
            return;
        }
        if (frame.frame_type == TM_C6_FRAME_LOG)
        {
#if defined(ESP_PLATFORM)
            const std::string message(frame.payload.begin(), frame.payload.end());
            ESP_LOGI(kTag, "C6 log: %s", message.c_str());
#endif
        }
    }

    bool receive_expected_frame(hostlink::c6::Frame& frame,
                                uint8_t expected_frame_type,
                                const char* expected_name,
                                uint32_t timeout_ms)
    {
#if defined(ESP_PLATFORM)
        std::lock_guard<std::recursive_mutex> receive_owner(receive_owner_mutex_);
        const TickType_t start_ticks = xTaskGetTickCount();
        uint32_t skipped = 0;
        while (true)
        {
            uint32_t remaining_ms = timeout_ms;
            if (timeout_ms > 0)
            {
                const uint32_t elapsed_ms =
                    static_cast<uint32_t>((xTaskGetTickCount() - start_ticks) * portTICK_PERIOD_MS);
                if (elapsed_ms >= timeout_ms)
                {
                    set_detail(status_, CompanionState::Missing, "c6_wait_expected_timeout");
                    ESP_LOGW(kTag,
                             "C6 wait %s timed out expected=0x%02x skipped=%lu",
                             expected_name,
                             static_cast<unsigned>(expected_frame_type),
                             static_cast<unsigned long>(skipped));
                    return false;
                }
                remaining_ms = timeout_ms - elapsed_ms;
            }

            hostlink::c6::Frame candidate{};
            if (!receive_frame(candidate, remaining_ms))
            {
                return false;
            }
            if (candidate.frame_type == expected_frame_type)
            {
                frame = candidate;
                if (skipped > 0)
                {
                    ESP_LOGI(kTag,
                             "C6 wait %s matched after async frames skipped=%lu seq=%u ack=%u payload_len=%u",
                             expected_name,
                             static_cast<unsigned long>(skipped),
                             static_cast<unsigned>(candidate.seq),
                             static_cast<unsigned>(candidate.ack),
                             static_cast<unsigned>(candidate.payload.size()));
                }
                return true;
            }

            ++skipped;
            ESP_LOGI(kTag,
                     "C6 wait %s handling async frame type=0x%02x channel=%u seq=%u ack=%u payload_len=%u",
                     expected_name,
                     static_cast<unsigned>(candidate.frame_type),
                     static_cast<unsigned>(candidate.channel),
                     static_cast<unsigned>(candidate.seq),
                     static_cast<unsigned>(candidate.ack),
                     static_cast<unsigned>(candidate.payload.size()));
            handle_async_frame(candidate);
            if (candidate.frame_type == TM_C6_FRAME_ERROR)
            {
                return false;
            }
        }
#else
        (void)frame;
        (void)expected_frame_type;
        (void)expected_name;
        (void)timeout_ms;
        return false;
#endif
    }

    bool try_handshake()
    {
#if defined(ESP_PLATFORM)
        transport_ = std::make_unique<SdioC6Transport>();
        if (!transport_->begin())
        {
            set_detail(status_, CompanionState::Missing, transport_->last_error());
            ESP_LOGW(kTag, "C6 SDIO transport unavailable detail=%s", status_.detail);
            transport_.reset();
            return false;
        }

        if (!send_hello())
        {
            ESP_LOGW(kTag, "C6 HELLO send failed detail=%s", status_.detail);
            transport_.reset();
            return false;
        }

        hostlink::c6::Frame frame{};
        if (!receive_expected_frame(frame, TM_C6_FRAME_HELLO_ACK, "HELLO_ACK", kHandshakeTimeoutMs) ||
            !handle_hello_ack(frame))
        {
            ESP_LOGW(kTag,
                     "C6 HELLO_ACK failed detail=%s rx_size=%lu rx_err=%s size_read=%u",
                     status_.detail,
                     transport_ ? static_cast<unsigned long>(transport_->last_rx_size()) : 0ul,
                     transport_ ? esp_err_to_name(static_cast<esp_err_t>(transport_->last_rx_error())) : "transport_missing",
                     transport_ ? static_cast<unsigned>(transport_->last_size_read()) : 0u);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!send_ping())
        {
            ESP_LOGW(kTag, "C6 PING send failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!receive_expected_frame(frame, TM_C6_FRAME_PONG, "PONG", kHandshakeTimeoutMs) || !handle_pong(frame))
        {
            ESP_LOGW(kTag, "C6 PONG failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!send_config_set())
        {
            ESP_LOGW(kTag, "C6 CONFIG_SET send failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        if (!receive_expected_frame(frame, TM_C6_FRAME_CONFIG_REPORT, "CONFIG_REPORT", kHandshakeTimeoutMs) ||
            !handle_config_report(frame))
        {
            ESP_LOGW(kTag, "C6 CONFIG_REPORT failed detail=%s", status_.detail);
            transport_->reset();
            transport_.reset();
            return false;
        }

        set_detail(status_, CompanionState::Present, "hello_ack_ping_pong_config_ok");
        ESP_LOGI(kTag,
                 "C6 present proto=%u fw=0x%08lx features=0x%08lx enabled=0x%08lx heap=%lu pongs=%lu",
                 status_.selected_protocol,
                 static_cast<unsigned long>(status_.firmware_version),
                 static_cast<unsigned long>(status_.supported_features),
                 static_cast<unsigned long>(status_.enabled_features),
                 static_cast<unsigned long>(status_.free_heap),
                 static_cast<unsigned long>(status_.pong_count));
        return true;
#else
        return false;
#endif
    }

    C6CompanionStatus status_{};
    uint16_t next_seq_ = kInitialSequence;
    uint32_t next_config_seq_ = kDefaultConfigSequence;
    BleCompanionConfig ble_config_{};
    EspNowCompanionConfig espnow_config_{};
    WifiCompanionConfig wifi_config_{};
    WifiTcpStatus tcp_status_{};
    uint8_t next_tcp_connection_id_ = 1;
    std::recursive_mutex receive_owner_mutex_{};
    std::mutex send_mutex_{};
    std::mutex tcp_rx_mutex_{};
    std::array<uint8_t, kC6RxStreamCapacity> frame_rx_stream_{};
    size_t frame_rx_stream_size_ = 0;
    uint32_t frame_rx_resync_count_ = 0;
    std::array<uint8_t, TM_C6_MAX_PAYLOAD> wifi_tcp_tx_buffer_{};
    // The SDIO transport stages bytes through its separate internal DMA
    // scratch. TCP data has no DMA requirement and belongs to an open session.
    using TcpRxBuffer = std::array<uint8_t, kWifiTcpRxBufferSize>;
    ::platform::memory::PsramPtr<TcpRxBuffer> tcp_rx_buffer_{};
    size_t tcp_rx_head_ = 0;
    size_t tcp_rx_tail_ = 0;
    size_t tcp_rx_size_ = 0;
#if defined(ESP_PLATFORM)
    std::unique_ptr<C6Transport> transport_;
#endif
};

C6CompanionRuntime s_c6_companion{};

} // namespace

WirelessCompanion& c6_companion()
{
    return s_c6_companion;
}

WifiTcpTransport& c6_wifi_tcp_transport()
{
    return s_c6_companion;
}

bool ensure_c6_companion_started()
{
    return c6_companion().begin();
}

bool enter_c6_companion_download_mode()
{
    return s_c6_companion.enterDownloadMode();
}

C6CompanionStatus get_c6_companion_status()
{
    return c6_companion().status();
}

const char* companion_state_name(CompanionState state)
{
    switch (state)
    {
    case CompanionState::Unsupported:
        return "unsupported";
    case CompanionState::NotStarted:
        return "not_started";
    case CompanionState::Missing:
        return "missing";
    case CompanionState::TransportPending:
        return "transport_pending";
    case CompanionState::Present:
        return "present";
    case CompanionState::Error:
        return "error";
    }
    return "unknown";
}

} // namespace platform::esp::idf_common::wireless_companion
