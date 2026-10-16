#include "memory_storage.h"
#include "music/player.h"
#include <cassert>
#include <fstream>
#include <iterator>
struct Output : music::AudioOutput
{
    bool occupied = false, opened = false, interrupt_on_write = false;
    unsigned frames = 0, begins = 0, ends = 0;
    bool supported() const override { return true; }
    bool blocked() const override { return occupied; }
    bool begin(uint32_t rate) override
    {
        assert(!occupied && rate >= 8000);
        opened = true;
        ++begins;
        return true;
    }
    bool write(const int16_t* pcm, std::size_t count) override
    {
        assert(opened && pcm && count);
        if (interrupt_on_write)
        {
            occupied = true;
            return false;
        }
        ++frames;
        return true;
    }
    void end() override
    {
        assert(opened);
        opened = false;
        ++ends;
    }
};
struct Images : music::ImageLoader
{
    unsigned decodes = 0;
    music::Artwork* decode(music::Reader&, const music::Track&, uint16_t, bool (*)(void*), void*, music::CoverResult& result) override
    {
        ++decodes;
        result = music::CoverResult::Missing;
        return nullptr;
    }
};
int main(int argc, char** argv)
{
    assert(argc == 2);
    MemoryStorage storage;
    std::ifstream file(argv[1], std::ios::binary);
    assert(file);
    MemoryStorage::Bytes bytes(std::istreambuf_iterator<char>(file), {});
    storage.files["/Music/first.mp3"] = bytes;
    storage.files["/Music/second.mp3"] = bytes;
    Output output;
    Images images;
    auto* player = music::make<music::Player>(storage, output, images);
    assert(player);
    player->attach(0);
    for (unsigned i = 0; i < 3; i++) player->tick();
    assert(storage.opens == 0 && !output.opened && !player->active());
    player->attach(3);
    player->tick();
    assert(player->active() && player->state.busy);
    player->attach(0);
    assert(!player->active() && !player->state.busy); // Hidden browsing cannot keep scanning SD.
    player->attach(3);
    for (unsigned i = 0; i < 12; i++) player->tick();
    assert(player->state.total == 2 && !player->state.busy);
    music::Request request;
    request.action = music::Action::Play;
    request.id = player->state.rows[0].id;
    assert(player->command(request));
    player->tick();
    assert(output.opened && player->state.playback == music::Playback::Playing && output.frames > 0 && images.decodes == 0);
    output.interrupt_on_write = true;
    player->tick();
    assert(!output.opened && player->active() && player->state.playback == music::Playback::Interrupted);
    output.interrupt_on_write = false;
    const auto before_focus = storage.opens;
    player->request_artwork(96);
    player->tick();
    assert(storage.opens == before_focus && images.decodes == 0);
    player->request_artwork(0);
    output.occupied = true;
    player->tick();
    assert(!output.opened && player->state.playback == music::Playback::Interrupted);
    output.occupied = false;
    player->tick();
    assert(output.opened && player->state.playback == music::Playback::Playing);
    output.occupied = true;
    player->tick();
    request.action = music::Action::TogglePause;
    assert(player->command(request));
    output.occupied = false;
    player->tick();
    assert(!output.opened && player->state.playback == music::Playback::Paused);
    request.action = music::Action::TogglePause;
    assert(player->command(request));
    player->tick();
    assert(output.opened);
    request.action = music::Action::SetPlayMode;
    request.value = 1;
    assert(player->command(request));
    const auto first = player->state.current.id;
    for (unsigned i = 0; i < 40; i++) player->tick();
    assert(player->state.current.id == first && player->active());
    request.action = music::Action::Next;
    assert(player->command(request));
    player->tick();
    assert(player->state.current.id != first);
    player->request_artwork(96);
    player->tick();
    assert(images.decodes == 1);
    player->request_artwork(0);
    for (unsigned i = 0; i < 4; i++) player->tick();
    assert(images.decodes == 1 && "hidden cover is not decoded");
    request.action = music::Action::TogglePause;
    assert(player->command(request));
    player->tick();
    assert(!output.opened);
    const auto paused_track = player->state.current.id;
    request.action = music::Action::Next;
    assert(player->command(request));
    player->tick();
    assert(!output.opened && player->state.playback == music::Playback::Paused && player->state.current.id != paused_track);
    auto* saved = music::make<music::ResumeState>();
    player->save_resume(*saved);
    auto* reopened = music::make<music::Player>(storage, output, images);
    reopened->restore_resume(*saved);
    assert(reopened->state.current.id == saved->current.id && reopened->state.playback == music::Playback::Paused);
    music::destroy(reopened);
    ++storage.media_session;
    reopened = music::make<music::Player>(storage, output, images);
    reopened->restore_resume(*saved);
    assert(reopened->state.current.id == 0 && reopened->state.playback == music::Playback::Stopped);
    music::destroy(reopened);
    music::destroy(saved);
    request.action = music::Action::Stop;
    assert(player->command(request));
    assert(!output.opened && !player->active());
    player->attach(0);
    const auto opens = storage.opens;
    for (unsigned i = 0; i < 4; i++) player->tick();
    assert(storage.opens == opens);
    music::destroy(player);
    assert(!output.opened && output.begins == output.ends);
}
