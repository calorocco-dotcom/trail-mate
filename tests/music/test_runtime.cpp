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
void run_worker()
{
    assert(runtime_test::scheduled);
    auto task = runtime_test::scheduled;
    runtime_test::scheduled = nullptr;
    task(runtime_test::task_context);
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
bool audio_begin(uint32_t)
{
    assert(!audio_open);
    audio_open = true;
    return true;
}
bool audio_write(const int16_t*, std::size_t)
{
    assert(audio_open);
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
    // Run real playback; inject a user pause/hide after its first output batch.
    const auto delays_before_playback = runtime_test::delays;
    const auto yields_before_playback = runtime_test::yields;
    send(music::Action::Play, library.rows[0].id);
    runtime_test::on_delay = []
    {
        if (!audio_open) return;
        runtime_test::on_delay = nullptr;
        send(music::Action::TogglePause);
        service->detach();
    };
    run_worker();
    assert(!audio_open && runtime_test::storages == 0 && allocations.size() == 1);
    assert(runtime_test::delays == delays_before_playback && runtime_test::yields > yields_before_playback);
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
