#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "music/cover_decoder.h"
#include "music/player.h"
#include "platform/audio/media_volume.h"
#include "platform/esp/common/music/music_ports.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/music_runtime.h"
#include "platform/ui/settings_store.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
namespace
{
std::map<void*, std::size_t> allocations;
void* resume_memory = nullptr;
bool resume_alive = false;
music::IService* service = nullptr;
int stored_volume = 45;
bool audio_open = false;
bool audio_available = true;
unsigned audio_writes = 0, underruns = 0, ui_refreshes = 0;
uint32_t audio_rate = 0, dma_deadline = 0;
uint32_t ui_revision = 0, ui_revision_at = 0;
unsigned progress_refreshes = 0;
void run_worker()
{
    assert(runtime_test::scheduled);
#if defined(ARDUINO_WIO_TRACKER_L2)
    assert(runtime_test::pinned_core == 0);
#else
    assert(runtime_test::pinned_core == -1);
#endif
    auto task = runtime_test::scheduled;
    runtime_test::scheduled = nullptr;
    task(runtime_test::task_context);
    assert(runtime_test::priority == 1); // Pausing/retiring releases the playback priority.
}
music::Snapshot snapshot()
{
    music::Snapshot out;
    assert(service->snapshot(out));
    return out;
}
void send(music::Action action, uint64_t id = 0)
{
    music::Request request;
    request.action = action;
    request.id = id;
    assert(service->send(request));
}
} // namespace
void* heap_caps_calloc(std::size_t count, std::size_t bytes, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    void* memory = std::calloc(count, bytes);
    assert(memory);
    allocations[memory] = count * bytes;
    if (count * bytes == sizeof(music::ResumeState))
    {
        resume_memory = memory;
        resume_alive = true;
    }
    return memory;
}
void heap_caps_free(void* memory)
{
    if (!memory) return;
    if (memory == resume_memory) resume_alive = false;
    assert(allocations.erase(memory) == 1);
    std::free(memory);
}
namespace platform::ui::settings_store
{
int get_int(const char*, const char*, int) { return stored_volume; }
void put_int(const char*, const char*, int value) { stored_volume = value; }
} // namespace platform::ui::settings_store
namespace platform::ui::device
{
void set_message_tone_volume(uint8_t value) { platform::audio::publish_media_volume(value); }
} // namespace platform::ui::device
namespace platform::esp::music_runtime
{
bool audio_supported() { return true; }
bool audio_begin(uint32_t rate)
{
    assert(!audio_open);
    if (!audio_available) return false;
    audio_open = true;
    audio_rate = rate;
    audio_writes = 0;
    return true;
}
bool audio_write(const int16_t*, std::size_t samples)
{
    assert(audio_open);
    if (audio_writes && runtime_test::tick > dma_deadline) ++underruns;
    // Model a blocking PCM output, leaving L2's 512-frame DMA queue full.
    runtime_test::tick += samples * 1000 / audio_rate;
    dma_deadline = runtime_test::tick + 512 * 1000 / audio_rate;
    ++audio_writes;
    return true;
}
void audio_end() { audio_open = false; }
} // namespace platform::esp::music_runtime
namespace music
{
Artwork* decode_cover(Reader&, const Track&, uint16_t, CoverResult& result, bool (*)(void*), void*)
{
    result = CoverResult::Missing;
    return nullptr;
}
} // namespace music
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    runtime_test::files["/Music/test.mp3"] = {std::istreambuf_iterator<char>(input), {}};
    service = platform::ui::music_service();
    service->attach(0);
    assert(!runtime_test::scheduled && runtime_test::storages == 0 && allocations.empty());
    music::Request volume;
    volume.action = music::Action::Volume;
    volume.value = 38;
    assert(service->send(volume) && service->volume() == 38);
    assert(!runtime_test::scheduled && allocations.empty());
    service->attach(3);
    run_worker();
    const auto library = snapshot();
    assert(library.total == 1);
    service->detach();
    run_worker();
    assert(allocations.empty() && runtime_test::storages == 0);
    service->attach(3);
    run_worker();
    assert(snapshot().total == 1);
    // A failed output open must not retain a realtime priority lease.
    audio_available = false;
    send(music::Action::Play, library.rows[0].id);
    run_worker();
    assert(!audio_open && snapshot().playback == music::Playback::Error);
    audio_available = true;
    // A ready UI task (default priority 1) draws for 30 ms between PCM writes.
    // At equal priority it preempts music long enough to empty L2's queue.
    // Playback must keep its priority lease until stopping, then release it.
    const auto delays_before_playback = runtime_test::delays;
    const auto yields_before_playback = runtime_test::yields;
    send(music::Action::Play, library.rows[0].id);
    runtime_test::on_delay = []
    {
        if (!audio_open) return;
        ++ui_refreshes;
        const auto worker_priority = runtime_test::priority;
        runtime_test::priority = 1; // Read the cached view from the UI core.
        const auto view = snapshot();
        runtime_test::priority = worker_priority;
        assert(view.playback == music::Playback::Playing);
        if (ui_refreshes == 1)
        {
            ui_revision = view.revision;
            ui_revision_at = runtime_test::tick;
        }
        else if (runtime_test::tick - ui_revision_at < 100) assert(view.revision == ui_revision);
        else if (view.revision != ui_revision)
        {
            ++progress_refreshes;
            ui_revision = view.revision;
            ui_revision_at = runtime_test::tick;
        }
        if (runtime_test::priority <= 1) runtime_test::tick += 30;
        if (ui_refreshes < 3)
        {
            // A snapshot reader on the other core still owns the publication
            // mutex. Raising priority alone cannot prevent this inversion.
            runtime_test::ui_busy_until = runtime_test::tick + 30;
            if (ui_refreshes == 1)
            {
                platform::audio::publish_media_volume(67);
                assert(service->volume() == 67); // Atomic volume is not held up by UI publication.
            }
            return;
        }
        runtime_test::ui_busy_until = 0;
        if (ui_refreshes < 6) return;
        runtime_test::on_delay = nullptr;
        send(music::Action::TogglePause);
        service->detach();
    };
    run_worker();
    assert(!audio_open && runtime_test::storages == 0 && allocations.size() == 1);
    assert(runtime_test::delays == delays_before_playback && runtime_test::yields > yields_before_playback);
    assert(ui_refreshes == 6 && underruns == 0 && audio_writes >= 6 && progress_refreshes > 0);
    assert(runtime_test::busy_skips > 0 && runtime_test::playback_lock_waits == 0);
    assert(snapshot().playback == music::Playback::Paused && resume_alive);
    // Interleave UI media invalidation at restore's first SD readiness operation.
    // A transferred record must stay alive until the worker has finished reading it.
    runtime_test::ready_calls = 0;
    runtime_test::on_ready = []
    {
        if (runtime_test::ready_calls != 2) return; // First call belongs to Player construction.
        runtime_test::on_ready = nullptr;
        assert(snapshot().playback == music::Playback::Paused);
        ++runtime_test::session;
        const auto out = snapshot();
        assert(out.playback == music::Playback::Stopped);
        assert(resume_alive);
    };
    service->attach(0);
    run_worker();
    assert(!runtime_test::on_ready);
    assert(!resume_alive && snapshot().playback == music::Playback::Stopped);
    service->detach();
    run_worker();
    assert(allocations.empty() && runtime_test::storages == 0 && !runtime_test::scheduled && !audio_open);
    std::puts("Actual ESP music proxy: lazy lifetime, idle volume, hidden pause cleanup and interleaved media invalidation passed.");
}
