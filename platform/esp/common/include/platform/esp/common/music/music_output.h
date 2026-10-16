#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "music/pcm_buffer.h"
#include "music/player.h"
#include <atomic>

namespace platform::esp::music_runtime
{
struct AudioState
{
    std::atomic<bool> opening{false}, active{false};
};

// The decoder owns begin/write/end. Only the consumer writes the hardware;
// it never touches SD, cover decoders, settings or the UI publication mutex.
class Output final : public ::music::AudioOutput
{
    AudioState& shared_;
    const std::atomic<uint8_t>& volume_;
    ::music::PcmBuffer pcm_;
    TaskHandle_t consumer_ = nullptr, producer_ = nullptr;
    StaticSemaphore_t done_storage_{};
    SemaphoreHandle_t done_ = nullptr;
    std::atomic<bool> stopping_{false}, interrupted_{false}, failed_{false};
    std::atomic<bool> draining_{false}, drained_{false};
    uint32_t rate_ = 0;
    UBaseType_t previous_priority_ = 0;
    bool priority_raised_ = false;
    // Output lives inside the PSRAM Runtime, including its conversion scratch.
    int16_t scratch_[256]{};
    uint32_t underruns_ = 0, minimum_ = 0;
    static void task(void* context);
    void consume();

  public:
    explicit Output(AudioState& state, const std::atomic<uint8_t>& volume) : shared_(state), volume_(volume) {}
    ~Output() override { end(); }
    bool supported() const override;
    bool blocked() const override;
    bool needs_frame() const override;
    bool failed() const override { return failed_.load(); }
    uint32_t pending_ms() const override;
    bool drain() override;
    bool ready_for_optional_work() const override;
    bool applies_volume() const override { return true; }
    bool begin(uint32_t rate) override;
    bool write(const int16_t* samples, std::size_t count) override;
    void end() override;
    bool waiting() const { return draining_.load() || !needs_frame(); }
};
} // namespace platform::esp::music_runtime
