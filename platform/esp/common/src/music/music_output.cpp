#include "platform/esp/common/music/music_output.h"
#include "platform/audio/audio_focus.h"
#include "platform/esp/common/music/music_ports.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace platform::esp::music_runtime
{
bool Output::supported() const { return audio_supported(); }
bool Output::blocked() const { return interrupted_.load() || ::platform::audio::music_blocked(); }
bool Output::needs_frame() const { return failed_.load() || draining_.load() || pcm_.writable(); }
uint32_t Output::pending_ms() const { return rate_ ? uint64_t(pcm_.size()) * 1000 / rate_ : 0; }
bool Output::ready_for_optional_work() const { return draining_.load() || pending_ms() >= 1500; }
bool Output::drain()
{
    draining_.store(true);
    if (consumer_) xTaskNotifyGive(consumer_);
    return drained_.load();
}
bool Output::begin(uint32_t rate)
{
    end();
    if (rate < 8000 || rate > 48000 || blocked() || !pcm_.open()) return false;
    rate_ = rate;
    done_ = xSemaphoreCreateBinaryStatic(&done_storage_);
    producer_ = xTaskGetCurrentTaskHandle();
    stopping_.store(false);
    interrupted_.store(false);
    failed_.store(false);
    draining_.store(false);
    drained_.store(false);
    underruns_ = 0;
    minimum_ = ::music::PcmBuffer::kSamples;
    shared_.opening.store(true);
    const bool opened = !blocked() && audio_begin(rate);
    shared_.active.store(opened);
    if (opened)
    {
        previous_priority_ = uxTaskPriorityGet(nullptr);
        priority_raised_ = previous_priority_ < tskIDLE_PRIORITY + 3;
        if (priority_raised_) vTaskPrioritySet(nullptr, tskIDLE_PRIORITY + 3);
#if defined(ARDUINO_WIO_TRACKER_L2)
        const auto created = xTaskCreatePinnedToCore(task, "music_pcm", 4096, this, tskIDLE_PRIORITY + 5, &consumer_, 0);
#else
        const auto created = xTaskCreate(task, "music_pcm", 4096, this, tskIDLE_PRIORITY + 5, &consumer_);
#endif
        if (created != pdPASS)
        {
            consumer_ = nullptr;
            audio_end();
            shared_.active.store(false);
        }
    }
    shared_.opening.store(false);
    if (!consumer_)
    {
        end();
        return false;
    }
    std::printf("[Music] PCM PSRAM=%luB reserve=%lums rate=%lu stack=4096B\n",
                static_cast<unsigned long>(::music::PcmBuffer::kBytes),
                static_cast<unsigned long>(uint64_t(::music::PcmBuffer::kSamples) * 1000 / rate_),
                static_cast<unsigned long>(rate_));
    return true;
}
bool Output::write(const int16_t* samples, std::size_t count)
{
    if (!consumer_ || failed_.load() || blocked() || count > ::music::PcmBuffer::kMaxFrame || !pcm_.write(samples, count)) return false;
    xTaskNotifyGive(consumer_);
    return true;
}
void Output::end()
{
    if (consumer_)
    {
        stopping_.store(true);
        xTaskNotifyGive(consumer_);
        // The consumer closes the hardware before acknowledging. No queued
        // samples survive pause/seek/next or leak into the next focus owner.
        xSemaphoreTake(done_, portMAX_DELAY);
        vTaskDelete(consumer_);
        consumer_ = nullptr;
        std::printf("[Music] PCM underruns=%lu min_reserve=%lums\n",
                    static_cast<unsigned long>(underruns_),
                    static_cast<unsigned long>(rate_ ? uint64_t(minimum_) * 1000 / rate_ : 0));
    }
    pcm_.close();
    rate_ = 0;
    interrupted_.store(false);
    if (priority_raised_)
    {
        priority_raised_ = false;
        vTaskPrioritySet(nullptr, previous_priority_);
    }
}
void Output::task(void* context)
{
    static_cast<Output*>(context)->consume();
    // The producer owns task deletion. Keeping this handle valid until join
    // makes concurrent notification and early hardware failure safe.
    vTaskSuspend(nullptr);
}
void Output::consume()
{
    bool primed = false, starved = false;
    // More than the current boards' I2S DMA capacities. Sending a silent tail
    // makes EOF drain deterministic; closing afterwards can cut only silence.
    uint32_t tail = 4096;
    while (!stopping_.load() && !::platform::audio::music_blocked())
    {
        const auto queued = pcm_.size();
        if (!primed && (queued >= rate_ / 4 || draining_.load())) primed = true;
        if (primed && queued)
        {
            if (!draining_.load()) minimum_ = std::min(minimum_, queued);
            const auto count = pcm_.copy(scratch_, 256, volume_.load());
            if (!audio_write(scratch_, count))
            {
                failed_.store(true);
                break;
            }
            pcm_.consume(count);
            starved = false;
            xTaskNotifyGive(producer_);
            continue;
        }
        if (primed && draining_.load() && !drained_.load())
        {
            const auto count = std::min<uint32_t>(256, tail);
            std::memset(scratch_, 0, sizeof(scratch_));
            if (!audio_write(scratch_, count))
            {
                failed_.store(true);
                break;
            }
            tail -= count;
            if (!tail)
            {
                drained_.store(true);
                xTaskNotifyGive(producer_);
            }
            continue;
        }
        if (primed && !draining_.load() && !starved)
        {
            ++underruns_;
            starved = true;
            primed = false; // Rebuild a reserve instead of repeatedly clicking.
        }
        // P4 uses a 10 ms RTOS tick: rounding this wait to zero would let the
        // priority-5 consumer spin forever and starve its priority-3 producer.
        ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(2)));
    }
    // Preserve the observed focus stop even if its reservation was released
    // or rolled back between leaving the loop and publishing completion.
    if (!stopping_.load() && !failed_.load()) interrupted_.store(true);
    audio_end();
    shared_.active.store(false);
    xTaskNotifyGive(producer_);
    // No access to this object after acknowledgement; its owner may retire it.
    xSemaphoreGive(done_);
}
} // namespace platform::esp::music_runtime
