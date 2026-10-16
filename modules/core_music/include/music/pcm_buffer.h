#pragma once
#include "music/memory.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace music
{
// One decoder producer and one hardware consumer. Publish complete PCM writes;
// release samples only after the sink has copied them into its DMA queue.
// open/close require both owners to be stopped. The payload is strictly PSRAM
// on ESP, with no internal-heap fallback.
class PcmBuffer
{
    int16_t* samples_ = nullptr;
    std::atomic<uint32_t> read_{0}, written_{0};

  public:
    static constexpr uint32_t kSamples = 128 * 1024;
    static constexpr uint32_t kBytes = kSamples * sizeof(int16_t);
    static constexpr uint32_t kMaxFrame = 1152;
    ~PcmBuffer() { close(); }
    PcmBuffer() = default;
    PcmBuffer(const PcmBuffer&) = delete;
    PcmBuffer& operator=(const PcmBuffer&) = delete;
    bool open()
    {
        close();
        samples_ = static_cast<int16_t*>(allocate(kBytes));
        return samples_ != nullptr;
    }
    void close()
    {
        release(samples_);
        samples_ = nullptr;
        read_.store(0, std::memory_order_relaxed);
        written_.store(0, std::memory_order_relaxed);
    }
    uint32_t size() const
    {
        const auto read = read_.load(std::memory_order_acquire);
        return std::min(written_.load(std::memory_order_acquire) - read, kSamples);
    }
    bool writable() const { return samples_ && kSamples - size() >= kMaxFrame; }
    bool write(const int16_t* input, uint32_t count)
    {
        const auto written = written_.load(std::memory_order_relaxed);
        if (!samples_ || !input || !count || count > kMaxFrame || count > kSamples - (written - read_.load(std::memory_order_acquire))) return false;
        const auto at = written % kSamples;
        const auto first = std::min(count, kSamples - at);
        std::memcpy(samples_ + at, input, first * sizeof(int16_t));
        std::memcpy(samples_, input + first, (count - first) * sizeof(int16_t));
        written_.store(written + count, std::memory_order_release);
        return true;
    }
    uint32_t copy(int16_t* output, uint32_t count, uint8_t volume) const
    {
        const auto read = read_.load(std::memory_order_relaxed);
        count = std::min(count, written_.load(std::memory_order_acquire) - read);
        volume = std::min<uint8_t>(volume, 100);
        for (uint32_t i = 0; i < count; ++i)
            output[i] = static_cast<int32_t>(samples_[(read + i) % kSamples]) * volume / 100;
        return count;
    }
    void consume(uint32_t count)
    {
        read_.store(read_.load(std::memory_order_relaxed) + count, std::memory_order_release);
    }
};
} // namespace music
