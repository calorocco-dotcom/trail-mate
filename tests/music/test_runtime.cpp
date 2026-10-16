#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "music/cover_decoder.h"
#include "music/player.h"
#include "platform/audio/audio_focus.h"
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
uint64_t expected_track = 0;
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
    // A widget must be able to observe new ID3 before potentially slow codec
    // opening, SD reads or cover decode, including queued Next commands.
    assert(expected_track && snapshot().current.id == expected_track);
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
    runtime_test::consumer_yield();
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
    runtime_test::files["/Music/other.mp3"] = runtime_test::files["/Music/test.mp3"];
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
    assert(library.total == 2);
    service->detach();
    run_worker();
    assert(allocations.empty() && runtime_test::storages == 0);
    service->attach(3);
    run_worker();
    assert(snapshot().total == 2);
    // A failed output open must not retain a realtime priority lease.
    audio_available = false;
    expected_track = library.rows[0].id;
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
        if (!audio_open || audio_writes == 0) return;
        ++ui_refreshes;
        const auto worker_priority = runtime_test::priority;
        runtime_test::priority = 1; // Read the cached view from the UI core.
        const auto view = snapshot();
        runtime_test::priority = worker_priority;
        assert(view.playback == music::Playback::Playing);
        if (ui_revision == 0)
        {
            ui_revision = view.revision;
            ui_revision_at = runtime_test::tick;
        }
        // Publication timestamps are rounded to the target's RTOS tick.
        else if (runtime_test::tick - ui_revision_at < 100 - 2 * kTestTickMs) assert(view.revision == ui_revision);
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
        if (ui_refreshes == 12)
        {
            const auto focus = platform::audio::request_focus(platform::audio::FocusOwner::Notification);
            assert(focus && !audio_open);
            platform::audio::release_focus(focus);
            ui_revision = 0; // Focus transitions intentionally force publication.
        }
        if (ui_refreshes == 18)
        {
            const auto current = snapshot().current.id;
            const auto library = snapshot();
            expected_track = library.rows[0].id == current ? library.rows[1].id : library.rows[0].id;
            assert(expected_track && expected_track != current);
            send(music::Action::Next);
            ui_revision = 0;
        }
        if (ui_refreshes < 30) return;
        runtime_test::on_delay = nullptr;
        send(music::Action::TogglePause);
        service->detach();
    };
    run_worker();
    assert(!audio_open && runtime_test::storages == 0 && allocations.size() == 1);
    assert(runtime_test::delays >= delays_before_playback && runtime_test::yields > yields_before_playback);
    assert(ui_refreshes == 30 && underruns == 0 && audio_writes >= 10 && progress_refreshes > 0);
    assert(runtime_test::consumer_starts == runtime_test::consumer_deletes);
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
