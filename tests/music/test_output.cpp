#include "esp_heap_caps.h"
#include "music/pcm_buffer.h"
#include "platform/audio/audio_focus.h"
#include "platform/esp/common/music/music_output.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#ifndef _WIN32
#include <thread>
#endif

namespace
{
std::map<void*, std::size_t> allocations;
bool fail_memory = false, opened = false, fail_write = false;
uint32_t rate = 0, deadline = 0, underruns = 0, writes = 0;
uint64_t frames = 0;
std::vector<int16_t> rendered;
constexpr uint32_t kConcurrentSamples = 2 * 1024 * 1024;
void produce_ordered(music::PcmBuffer& pcm)
{
    int16_t frame[1024];
    for (uint32_t produced = 0; produced < kConcurrentSamples; produced += 1024)
    {
        for (unsigned i = 0; i < 1024; ++i) frame[i] = (produced + i) % 30000;
        while (!pcm.write(frame, 1024))
        {
        }
    }
}
#ifdef _WIN32
DWORD WINAPI produce_thread(void* context)
{
    produce_ordered(*static_cast<music::PcmBuffer*>(context));
    return 0;
}
#endif
} // namespace
void* heap_caps_calloc(std::size_t count, std::size_t bytes, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_memory) return nullptr;
    auto* p = std::calloc(count, bytes);
    assert(p);
    allocations[p] = count * bytes;
    return p;
}
void heap_caps_free(void* p)
{
    if (!p) return;
    assert(allocations.erase(p) == 1);
    std::free(p);
}
namespace platform::esp::music_runtime
{
bool audio_supported() { return true; }
bool audio_begin(uint32_t hz)
{
    assert(!opened);
    opened = true;
    rate = hz;
    deadline = writes = 0;
    frames = 0;
    rendered.clear();
    return true;
}
bool audio_write(const int16_t* pcm, std::size_t count)
{
    assert(opened && count && count <= 256 && runtime_test::in_consumer);
    if (fail_write) return false;
    if (writes && runtime_test::tick > deadline) ++underruns;
    rendered.insert(rendered.end(), pcm, pcm + count);
    const auto previous = frames * 1000 / rate;
    frames += count;
    runtime_test::tick += frames * 1000 / rate - previous;
    deadline = runtime_test::tick + 512 * 1000 / rate;
    ++writes;
    runtime_test::consumer_yield();
    return true;
}
void audio_end()
{
    assert(opened);
    opened = false;
}
} // namespace platform::esp::music_runtime

int main()
{
    using platform::esp::music_runtime::AudioState;
    using platform::esp::music_runtime::Output;
    AudioState state;
    std::atomic<uint8_t> volume{50};
    auto* output = music::make<Output>(state, volume);
    const auto base_allocations = allocations.size();
    assert(base_allocations == 1 && !opened);
    fail_memory = true;
    assert(!output->begin(48000) && allocations.size() == base_allocations);
    fail_memory = false;
    runtime_test::fail_consumer_task = true;
    assert(!output->begin(48000) && !opened && !state.active && runtime_test::priority == 1);
    assert(allocations.size() == base_allocations);
    runtime_test::fail_consumer_task = false;
    assert(output->begin(48000));
    assert(allocations.size() == base_allocations + 1);
    assert(!output->ready_for_optional_work());
#if defined(ARDUINO_WIO_TRACKER_L2)
    assert(runtime_test::consumer_core == 0);
#endif
    int16_t frame[1024];
    for (auto& sample : frame) sample = 12000;
    for (unsigned i = 0; i < 128; ++i) assert(output->write(frame, 1024));
    assert(!output->needs_frame() && output->ready_for_optional_work());
    assert(!output->write(frame, 1024)); // Full: reject the entire write.
    assert(output->pending_ms() == 2730);
    // No decoding, file reads, UI publication or producer task for 600 ms.
    // An enlarged buffer without an independent consumer cannot pass this.
    const auto before = runtime_test::tick;
    runtime_test::advance(600);
    assert(runtime_test::tick - before >= 600 && rendered.size() >= 48000 * 6 / 10);
    assert(underruns == 0 && output->pending_ms() >= 2100);
    for (auto sample : rendered) assert(sample == 6000);
    // Gain is applied at render time, including already queued raw PCM.
    volume.store(17);
    runtime_test::advance(20);
    assert(rendered.back() == 2040 && underruns == 0);
    volume.store(0);
    runtime_test::advance(20);
    assert(rendered.back() == 0);
    const auto writes_before_pause = writes;
    const auto pause_at = runtime_test::tick;
    output->end();
    assert(!opened && !state.active && allocations.size() == base_allocations);
    assert(writes == writes_before_pause && runtime_test::tick == pause_at); // Discard, never drain on pause.
    assert(runtime_test::priority == 1);
    // EOF must play a short clip that cannot reach the 250 ms prebuffer target.
    volume.store(100);
    assert(output->begin(44100));
    assert(output->write(frame, 31));
    runtime_test::advance(20);
    assert(rendered.empty());
    while (!output->drain()) runtime_test::advance(5);
    output->end();
    assert(rendered.size() == 31 + 4096);
    for (unsigned i = 0; i < 31; ++i) assert(rendered[i] == 12000);
    for (std::size_t i = 31; i < rendered.size(); ++i) assert(rendered[i] == 0);
    // Preemption works even while the producer is stuck elsewhere. The latch
    // survives focus release until Player acknowledges and closes the stream.
    assert(output->begin(48000));
    for (unsigned i = 0; i < 100; ++i) assert(output->write(frame, 1024));
    runtime_test::advance(20);
    const auto focus = platform::audio::request_focus(platform::audio::FocusOwner::Notification);
    assert(focus);
    runtime_test::advance(1);
    assert(!opened && !state.active && output->blocked());
    platform::audio::release_focus(focus);
    assert(output->blocked() && !output->write(frame, 1024));
    output->end();
    assert(!output->blocked());
    // No samples from the preempted song survive into a subsequent session.
    assert(output->begin(48000));
    for (auto& sample : frame) sample = -1234;
    for (unsigned i = 0; i < 20; ++i) assert(output->write(frame, 1024));
    runtime_test::advance(20);
    assert(!rendered.empty());
    for (auto sample : rendered) assert(sample == -1234);
    fail_write = true;
    runtime_test::advance(1);
    assert(!opened && !state.active && output->failed() && !output->write(frame, 1024));
    output->end();
    fail_write = false;
    music::destroy(output);
    assert(allocations.empty() && runtime_test::consumer_starts == runtime_test::consumer_deletes);
    // Repeated wrap-around must preserve exact order and samples. The full
    // buffer rejects writes without replacing or partially publishing data.
    music::PcmBuffer pcm;
    assert(pcm.open());
    int16_t readback[256];
    uint32_t produced = 0, consumed = 0;
    for (unsigned i = 0; i < 8000; ++i)
    {
        if (pcm.writable())
        {
            for (unsigned j = 0; j < 1024; ++j) frame[j] = (produced + j) % 30000;
            assert(pcm.write(frame, 1024));
            produced += 1024;
        }
        const auto count = pcm.copy(readback, 256, 100);
        for (unsigned j = 0; j < count; ++j) assert(readback[j] == (consumed + j) % 30000);
        pcm.consume(count);
        consumed += count;
    }
    assert(produced > music::PcmBuffer::kSamples * 10 && consumed > music::PcmBuffer::kSamples * 10);
    pcm.close();
    // Exercise the same release/acquire publication with truly concurrent
    // owners too; the deterministic task scheduler cannot reveal a data race.
    assert(pcm.open());
#ifdef _WIN32
    auto producer = CreateThread(nullptr, 0, produce_thread, &pcm, 0, nullptr);
    assert(producer);
#else
    std::thread producer([&]
                         { produce_ordered(pcm); });
#endif
    consumed = 0;
    while (consumed < kConcurrentSamples)
    {
        const auto count = pcm.copy(readback, 256, 100);
        for (unsigned j = 0; j < count; ++j) assert(readback[j] == (consumed + j) % 30000);
        pcm.consume(count);
        consumed += count;
    }
#ifdef _WIN32
    assert(WaitForSingleObject(producer, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(producer);
#else
    producer.join();
#endif
    assert(pcm.size() == 0);
    pcm.close();
    assert(allocations.empty());
    std::puts("Production PCM consumer: PSRAM-only, 600ms producer stall, live gain, short EOF, immediate discard, focus latch, failure cleanup and ring wrap passed.");
}
