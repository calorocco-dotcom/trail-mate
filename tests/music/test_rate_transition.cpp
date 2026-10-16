#include "memory_storage.h"
#include "music/player.h"
#include <cassert>
#include <fstream>
#include <iterator>

// A deterministic decoder boundary drives a rate transition in the real
// Player, independently of the separate real MP3 decoder tests.
namespace music
{
struct Decoder::Workspace
{
    unsigned frame = 0;
    int16_t pcm[1152]{};
};
Decoder::Decoder() : state_(make<Workspace>()) {}
Decoder::~Decoder() { destroy(state_); }
bool Decoder::open(Reader&, Track&) { return state_ != nullptr; }
bool Decoder::seek(uint32_t) { return false; }
uint32_t Decoder::position_ms() const { return state_->frame * 25; }
DecodeResult Decoder::next(PcmFrame& out)
{
    if (state_->frame == 2) return DecodeResult::End;
    const auto rate = state_->frame++ ? 48000U : 22050U;
    state_->pcm[0] = state_->frame;
    out = {state_->pcm, 1152, rate};
    return DecodeResult::Frame;
}
} // namespace music
struct Output : music::AudioOutput
{
    uint32_t rate = 0, begins = 0, ends = 0, writes = 0, drains = 0;
    bool drained = false;
    bool supported() const override { return true; }
    bool blocked() const override { return false; }
    bool begin(uint32_t hz) override
    {
        assert(!rate);
        rate = hz;
        ++begins;
        return true;
    }
    bool write(const int16_t* pcm, std::size_t count) override
    {
        assert(count == 1152);
        assert((writes == 0 && rate == 22050 && pcm[0] == 1) || (writes == 1 && rate == 48000 && pcm[0] == 2));
        ++writes;
        return true;
    }
    bool drain() override
    {
        ++drains;
        return drained;
    }
    bool applies_volume() const override { return true; }
    void end() override
    {
        assert(rate);
        rate = 0;
        ++ends;
    }
};
struct Images : music::ImageLoader
{
    music::Artwork* decode(music::Reader&, const music::Track&, uint16_t, bool (*)(void*), void*, music::CoverResult&) override { return nullptr; }
};
int main(int argc, char** argv)
{
    assert(argc == 2);
    MemoryStorage storage;
    std::ifstream input(argv[1], std::ios::binary);
    storage.files["/Music/test.mp3"] = {std::istreambuf_iterator<char>(input), {}};
    Output output;
    Images images;
    auto* player = music::make<music::Player>(storage, output, images);
    player->attach(3);
    for (unsigned i = 0; i < 12; ++i) player->tick();
    music::Request request;
    request.action = music::Action::Play;
    request.id = player->state.rows[0].id;
    assert(player->command(request));
    player->tick();
    assert(output.begins == 1 && output.writes == 1);
    for (unsigned i = 0; i < 10; ++i) player->tick();
    assert(output.drains == 10 && output.begins == 1 && output.writes == 1 && output.ends == 0);
    output.drained = true;
    player->tick();
    assert(output.begins == 2 && output.writes == 2 && output.ends == 1);
    assert(player->state.position_ms == 50);
    music::destroy(player);
    assert(output.ends == 2 && !output.rate);
}
