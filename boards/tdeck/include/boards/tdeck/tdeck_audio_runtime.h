#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace boards::tdeck
{

class TDeckAudioRuntime
{
  public:
    bool begin();
    void requestMessageTone(uint8_t volume_percent);
    /**
     * @brief Decodes one Codec2-1300 VMP object and writes it to the speaker.
     *
     * This runs on the VMP worker, never the LVGL task.  It returns false if
     * the speaker is not ready or is currently playing a notification tone.
     */
    bool playCodec2Voice(const uint8_t* encoded_media,
                         std::size_t encoded_media_len,
                         uint8_t volume_percent);
    bool isReady() const;
    bool beginMusic(uint32_t sample_rate);
    bool writeMusic(const int16_t* mono, std::size_t samples);
    void endMusic();

  private:
    static constexpr std::size_t kPcmSampleCount = 128U * 2U;

    static void taskEntry(void* context);
    void taskLoop();
    void playTone(uint8_t volume_percent);

    TaskHandle_t task_ = nullptr;
    std::atomic<bool> ready_{false};
    std::atomic<bool> faulted_{false};
    std::atomic<uint8_t> volume_percent_{45};
    std::atomic_flag audio_busy_ = ATOMIC_FLAG_INIT;
    bool music_active_ = false; // Only the single music worker accesses its session.
    std::array<int16_t, kPcmSampleCount> pcm_{};
};

} // namespace boards::tdeck
