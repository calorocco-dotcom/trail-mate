#if defined(ARDUINO_T_DECK)

#include "boards/tdeck/tdeck_audio_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "pins_arduino.h"
#include "platform/audio/audio_focus.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/ui/audio/pager_notification_tone.h"

#include <codec2.h>
#include <driver/i2s.h>
#include <esp_err.h>
#include <esp_heap_caps.h>

namespace boards::tdeck
{
namespace
{

constexpr i2s_port_t kSpeakerI2sPort = static_cast<i2s_port_t>(DAC_I2S_PORT);
constexpr size_t kFramesPerChunk = 128;
constexpr size_t kDmaBufferCount = 2;
constexpr size_t kDmaBufferLength = 128;
constexpr size_t kAudioInternalReservation = 4096;
constexpr size_t kAudioDmaReservation = 8 * 1024;
constexpr size_t kAudioInternalFloor = 48 * 1024;
constexpr size_t kAudioDmaFloor = 16 * 1024;
// The notification tone's measured high-water mark left only 132-164 bytes
// on the original 2 KiB task stack while handling MQTT chat alerts.
constexpr uint32_t kAudioTaskStackBytes = 4096;
constexpr UBaseType_t kAudioTaskPriority = 2;
constexpr uint32_t kCodec2VoiceSampleRateHz = 8000U;
constexpr std::size_t kCodec2BytesPerFrame = 7U;
constexpr std::size_t kCodec2FramesPerMessage = 125U;
constexpr std::size_t kCodec2MaximumEncodedBytes =
    kCodec2BytesPerFrame * kCodec2FramesPerMessage;
constexpr std::size_t kCodec2SamplesPerFrame = 320U;
constexpr std::size_t kCodec2OutputChannels = 2U;
constexpr std::size_t kCodec2PcmBytes =
    kCodec2SamplesPerFrame * kCodec2OutputChannels * sizeof(int16_t);
// Codec2 creates several working blocks while decoding.  Reserve a generous
// PSRAM window for the complete temporary operation (PCM plus Codec2 state),
// so playback is refused before it can squeeze display/map allocations.
constexpr std::size_t kCodec2PsramReservation = 96U * 1024U;
constexpr std::size_t kCodec2InternalReservation = 0U;
constexpr std::size_t kCodec2DmaReservation = 0U;
constexpr std::size_t kCodec2InternalFloor = 48U * 1024U;
constexpr std::size_t kCodec2DmaFloor = 16U * 1024U;
constexpr std::size_t kCodec2PsramFloor = 256U * 1024U;
constexpr std::size_t kVoiceLevelLogIntervalFrames = 25U;

struct PcmLevel
{
    uint16_t peak = 0U;
    uint16_t rms = 0U;
};

PcmLevel measurePcmLevel(const int16_t* samples,
                         std::size_t sample_count,
                         std::size_t stride = 1U)
{
    if (!samples || sample_count == 0U)
    {
        return {};
    }

    uint32_t peak = 0U;
    uint64_t sum_of_squares = 0U;
    for (std::size_t index = 0U; index < sample_count; ++index)
    {
        const int32_t sample = samples[index * stride];
        const uint32_t magnitude =
            sample < 0 ? static_cast<uint32_t>(-sample) : static_cast<uint32_t>(sample);
        peak = std::max(peak, magnitude);
        sum_of_squares += static_cast<uint64_t>(sample * sample);
    }

    const float mean_square =
        static_cast<float>(sum_of_squares) / static_cast<float>(sample_count);
    return {static_cast<uint16_t>(peak),
            static_cast<uint16_t>(std::sqrt(mean_square) + 0.5F)};
}

bool shouldLogVoiceLevel(std::size_t frame_index, std::size_t frame_count)
{
    const std::size_t completed_frames = frame_index + 1U;
    return frame_index == 0U || (completed_frames % kVoiceLevelLogIntervalFrames) == 0U ||
           completed_frames == frame_count;
}

void logVoicePcmLevel(std::size_t frame_index,
                      std::size_t frame_count,
                      const PcmLevel& decoded,
                      const PcmLevel& output,
                      uint8_t volume_percent)
{
    std::printf("[TDeck][Audio] voice level frame=%u/%u decoded_peak=%u decoded_rms=%u "
                "output_peak=%u output_rms=%u volume=%u\n",
                static_cast<unsigned>(frame_index + 1U),
                static_cast<unsigned>(frame_count),
                static_cast<unsigned>(decoded.peak),
                static_cast<unsigned>(decoded.rms),
                static_cast<unsigned>(output.peak),
                static_cast<unsigned>(output.rms),
                static_cast<unsigned>(volume_percent));
}

void secureClear(int16_t* samples, std::size_t sample_count)
{
    volatile int16_t* cursor = samples;
    while (cursor && sample_count-- != 0U)
    {
        *cursor++ = 0;
    }
}

} // namespace

bool TDeckAudioRuntime::begin()
{
    if (ready_.load(std::memory_order_acquire))
    {
        return true;
    }
    if (faulted_.load(std::memory_order_acquire))
    {
        return false;
    }
    ::platform::esp::common::memory::logSnapshot("audio", "before_i2s");
    if (!::platform::esp::common::memory::admit("audio",
                                                kAudioInternalReservation,
                                                kAudioDmaReservation,
                                                0,
                                                kAudioInternalFloor,
                                                kAudioDmaFloor))
    {
        std::printf("[TDeck][Audio] unavailable before I2S install\n");
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    const i2s_config_t config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = ::platform::ui::audio::pager_notification::kPlaybackSampleRateHz,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = kDmaBufferCount,
        .dma_buf_len = kDmaBufferLength,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
#if defined(I2S_MCLK_MULTIPLE_256)
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
#endif
    };

    esp_err_t err = i2s_driver_install(kSpeakerI2sPort, &config, 0, nullptr);
    if (err != ESP_OK)
    {
        std::printf("[TDeck][Audio] unavailable I2S install err=%s\n",
                    esp_err_to_name(err));
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    const i2s_pin_config_t pins = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = DAC_I2S_BCK,
        .ws_io_num = DAC_I2S_WS,
        .data_out_num = DAC_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    err = i2s_set_pin(kSpeakerI2sPort, &pins);
    if (err != ESP_OK)
    {
        std::printf("[TDeck][Audio] unavailable I2S pins err=%s\n", esp_err_to_name(err));
        i2s_driver_uninstall(kSpeakerI2sPort);
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    err = i2s_start(kSpeakerI2sPort);
    if (err != ESP_OK)
    {
        std::printf("[TDeck][Audio] unavailable I2S start err=%s\n", esp_err_to_name(err));
        i2s_driver_uninstall(kSpeakerI2sPort);
        faulted_.store(true, std::memory_order_release);
        return false;
    }
    i2s_zero_dma_buffer(kSpeakerI2sPort);

    const BaseType_t task_result =
        xTaskCreatePinnedToCore(&TDeckAudioRuntime::taskEntry,
                                "tdeck_audio",
                                kAudioTaskStackBytes,
                                this,
                                kAudioTaskPriority,
                                &task_,
                                1);
    if (task_result != pdPASS)
    {
        std::printf("[TDeck][Audio] unavailable audio task create failed\n");
        i2s_stop(kSpeakerI2sPort);
        i2s_driver_uninstall(kSpeakerI2sPort);
        task_ = nullptr;
        faulted_.store(true, std::memory_order_release);
        return false;
    }

    ready_.store(true, std::memory_order_release);
    std::printf("[TDeck][Audio] runtime ready port=%u dma=%u/%u task_stack=%u\n",
                static_cast<unsigned>(kSpeakerI2sPort),
                static_cast<unsigned>(kDmaBufferCount),
                static_cast<unsigned>(kDmaBufferLength),
                static_cast<unsigned>(kAudioTaskStackBytes));
    ::platform::esp::common::memory::logSnapshot("audio", "after_i2s");
    return true;
}

void TDeckAudioRuntime::requestMessageTone(uint8_t volume_percent)
{
    if (volume_percent == 0U)
    {
        return;
    }
    volume_percent = std::min<uint8_t>(volume_percent, 100U);
    volume_percent_.store(volume_percent, std::memory_order_release);

    if (!ready_.load(std::memory_order_acquire) || task_ == nullptr)
    {
        std::printf("[TDeck][Audio] tone dropped runtime_unavailable volume=%u\n",
                    static_cast<unsigned>(volume_percent));
        return;
    }

    xTaskNotifyGive(task_);
}

bool TDeckAudioRuntime::playCodec2Voice(const uint8_t* encoded_media,
                                        std::size_t encoded_media_len,
                                        uint8_t volume_percent)
{
    if (!encoded_media || encoded_media_len == 0U ||
        encoded_media_len > kCodec2MaximumEncodedBytes ||
        encoded_media_len % kCodec2BytesPerFrame != 0U ||
        !ready_.load(std::memory_order_acquire) ||
        faulted_.load(std::memory_order_acquire))
    {
        return false;
    }
    if (!::platform::esp::common::memory::admit("vmp_play",
                                                kCodec2InternalReservation,
                                                kCodec2DmaReservation,
                                                kCodec2PsramReservation,
                                                kCodec2InternalFloor,
                                                kCodec2DmaFloor,
                                                kCodec2PsramFloor))
    {
        std::printf("[TDeck][Audio] voice rejected reason=memory_budget\n");
        return false;
    }
    platform::audio::FocusScope focus(platform::audio::FocusOwner::VoiceMessage);
    if (!focus) return false;
    if (audio_busy_.test_and_set(std::memory_order_acquire))
    {
        std::printf("[TDeck][Audio] voice rejected reason=audio_busy\n");
        return false;
    }

    bool complete = false;
    const uint8_t output_volume = std::min<uint8_t>(volume_percent, 100U);
    int16_t* const playback_pcm = static_cast<int16_t*>(
        heap_caps_malloc(kCodec2PcmBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!playback_pcm)
    {
        audio_busy_.clear(std::memory_order_release);
        std::printf("[TDeck][Audio] voice rejected reason=psram_unavailable\n");
        return false;
    }

    esp_err_t err = i2s_set_sample_rates(kSpeakerI2sPort, kCodec2VoiceSampleRateHz);
    if (err != ESP_OK)
    {
        std::printf("[TDeck][Audio] voice rejected reason=sample_rate err=%s\n",
                    esp_err_to_name(err));
    }
    else
    {
        CODEC2* const decoder = codec2_create(CODEC2_MODE_1300);
        const int sample_count = decoder ? codec2_samples_per_frame(decoder) : 0;
        const int byte_count = decoder ? codec2_bytes_per_frame(decoder) : 0;
        if (!decoder || sample_count != static_cast<int>(kCodec2SamplesPerFrame) ||
            byte_count != static_cast<int>(kCodec2BytesPerFrame))
        {
            if (decoder)
            {
                codec2_destroy(decoder);
            }
            std::printf("[TDeck][Audio] voice rejected reason=codec_failure\n");
        }
        else
        {
            codec2_set_lpc_post_filter(decoder, 1, 0, 0.8F, 0.2F);
            complete = true;
            const std::size_t frame_count = encoded_media_len / kCodec2BytesPerFrame;
            for (std::size_t frame_index = 0U, offset = 0U; offset < encoded_media_len;
                 ++frame_index, offset += kCodec2BytesPerFrame)
            {
                codec2_decode(decoder,
                              playback_pcm,
                              const_cast<uint8_t*>(encoded_media + offset));
                const bool log_level = shouldLogVoiceLevel(frame_index, frame_count);
                const PcmLevel decoded_level =
                    log_level ? measurePcmLevel(playback_pcm, kCodec2SamplesPerFrame) : PcmLevel{};
                // Expand backwards so mono samples are never overwritten before
                // each sample reaches both output channels.
                for (std::size_t sample = kCodec2SamplesPerFrame; sample != 0U;
                     --sample)
                {
                    const std::size_t mono_index = sample - 1U;
                    int32_t output_sample = playback_pcm[mono_index];
                    output_sample = output_sample * output_volume / 100;
                    playback_pcm[mono_index * kCodec2OutputChannels] =
                        static_cast<int16_t>(output_sample);
                    playback_pcm[mono_index * kCodec2OutputChannels + 1U] =
                        static_cast<int16_t>(output_sample);
                }

                if (log_level)
                {
                    logVoicePcmLevel(
                        frame_index,
                        frame_count,
                        decoded_level,
                        measurePcmLevel(playback_pcm, kCodec2SamplesPerFrame, kCodec2OutputChannels),
                        output_volume);
                }

                size_t bytes_written = 0U;
                err = i2s_write(kSpeakerI2sPort,
                                playback_pcm,
                                kCodec2PcmBytes,
                                &bytes_written,
                                pdMS_TO_TICKS(100));
                if (err != ESP_OK || bytes_written != kCodec2PcmBytes)
                {
                    std::printf("[TDeck][Audio] voice write failed err=%s requested=%u "
                                "written=%u\n",
                                esp_err_to_name(err),
                                static_cast<unsigned>(kCodec2PcmBytes),
                                static_cast<unsigned>(bytes_written));
                    complete = false;
                    break;
                }
            }
            codec2_destroy(decoder);
        }
    }

    secureClear(playback_pcm, kCodec2PcmBytes / sizeof(*playback_pcm));
    heap_caps_free(playback_pcm);
    i2s_zero_dma_buffer(kSpeakerI2sPort);
    const esp_err_t restore_err = i2s_set_sample_rates(
        kSpeakerI2sPort,
        ::platform::ui::audio::pager_notification::kPlaybackSampleRateHz);
    if (restore_err != ESP_OK)
    {
        std::printf("[TDeck][Audio] voice restore sample_rate failed err=%s\n",
                    esp_err_to_name(restore_err));
        complete = false;
        faulted_.store(true, std::memory_order_release);
        ready_.store(false, std::memory_order_release);
    }
    audio_busy_.clear(std::memory_order_release);

    std::printf("[TDeck][Audio] voice %s frames=%u bytes=%u volume=%u\n",
                complete ? "complete" : "failed",
                static_cast<unsigned>(encoded_media_len / kCodec2BytesPerFrame),
                static_cast<unsigned>(encoded_media_len),
                static_cast<unsigned>(output_volume));
    return complete;
}

bool TDeckAudioRuntime::isReady() const
{
    return ready_.load(std::memory_order_acquire) &&
           !faulted_.load(std::memory_order_acquire);
}
bool TDeckAudioRuntime::beginMusic(uint32_t sample_rate)
{
    if (!isReady() || platform::audio::music_blocked() || music_active_ || audio_busy_.test_and_set(std::memory_order_acquire)) return false;
    if (i2s_set_sample_rates(kSpeakerI2sPort, sample_rate) != ESP_OK)
    {
        audio_busy_.clear(std::memory_order_release);
        return false;
    }
    music_active_ = true;
    return true;
}
bool TDeckAudioRuntime::writeMusic(const int16_t* mono, std::size_t samples)
{
    if (!music_active_ || !mono) return false;
    for (std::size_t offset = 0; offset < samples;)
    {
        const auto count = std::min<std::size_t>(kFramesPerChunk, samples - offset);
        for (std::size_t i = 0; i < count; i++) pcm_[2 * i] = pcm_[2 * i + 1] = mono[offset + i];
        std::size_t written = 0;
        const auto bytes = count * 2 * sizeof(int16_t);
        if (i2s_write(kSpeakerI2sPort, pcm_.data(), bytes, &written, pdMS_TO_TICKS(100)) != ESP_OK || written != bytes) return false;
        offset += count;
    }
    return true;
}
void TDeckAudioRuntime::endMusic()
{
    if (!music_active_) return;
    i2s_zero_dma_buffer(kSpeakerI2sPort);
    (void)i2s_set_sample_rates(kSpeakerI2sPort, ::platform::ui::audio::pager_notification::kPlaybackSampleRateHz);
    music_active_ = false;
    audio_busy_.clear(std::memory_order_release);
}

void TDeckAudioRuntime::taskEntry(void* context)
{
    auto* runtime = static_cast<TDeckAudioRuntime*>(context);
    if (runtime != nullptr)
    {
        runtime->taskLoop();
    }
    vTaskDelete(nullptr);
}

void TDeckAudioRuntime::taskLoop()
{
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!ready_.load(std::memory_order_acquire))
        {
            continue;
        }
        platform::audio::FocusScope focus(platform::audio::FocusOwner::Notification);
        if (!focus) continue;
        if (audio_busy_.test_and_set(std::memory_order_acquire))
        {
            std::printf("[TDeck][Audio] tone dropped voice_playback_active\n");
            continue;
        }
        playTone(volume_percent_.load(std::memory_order_acquire));
        audio_busy_.clear(std::memory_order_release);
        const unsigned long stack_free =
            static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)) *
            sizeof(StackType_t);
        std::printf("[TDeck][Audio][Mem] task_stack_free=%lu\n", stack_free);
    }
}

void TDeckAudioRuntime::playTone(uint8_t volume_percent)
{
    namespace pager_tone = ::platform::ui::audio::pager_notification;
    const float gain = static_cast<float>(volume_percent) / 100.0f;
    pager_tone::AdpcmPlaybackState tone_state{};
    size_t total_frames = 0;
    size_t total_bytes = 0;

    while (pager_tone::hasMore(tone_state))
    {
        const uint16_t frames = pager_tone::fillStereoInterleaved(
            tone_state,
            pcm_.data(),
            static_cast<uint16_t>(kFramesPerChunk));
        if (frames == 0U)
        {
            break;
        }

        if (gain < 0.999f)
        {
            for (size_t index = 0;
                 index < static_cast<size_t>(frames) * pager_tone::kChannels;
                 ++index)
            {
                pcm_[index] =
                    static_cast<int16_t>(static_cast<float>(pcm_[index]) * gain);
            }
        }

        const size_t bytes =
            static_cast<size_t>(frames) * pager_tone::kChannels * sizeof(int16_t);
        size_t bytes_written = 0;
        const esp_err_t err = i2s_write(kSpeakerI2sPort,
                                        pcm_.data(),
                                        bytes,
                                        &bytes_written,
                                        pdMS_TO_TICKS(100));
        total_frames += bytes_written / (pager_tone::kChannels * sizeof(int16_t));
        total_bytes += bytes_written;
        if (err != ESP_OK || bytes_written != bytes)
        {
            std::printf("[TDeck][Audio] tone write failed err=%s requested=%u written=%u\n",
                        esp_err_to_name(err),
                        static_cast<unsigned>(bytes),
                        static_cast<unsigned>(bytes_written));
            return;
        }
    }

    std::printf("[TDeck][Audio] tone complete frames=%u bytes=%u\n",
                static_cast<unsigned>(total_frames),
                static_cast<unsigned>(total_bytes));
}

} // namespace boards::tdeck

#endif // defined(ARDUINO_T_DECK)
