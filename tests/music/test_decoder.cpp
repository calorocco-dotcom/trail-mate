#include "music/decoder.h"
#include "music/id3.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
struct File : music::Reader
{
    FILE* handle = nullptr;
    uint32_t length = 0;
    explicit File(const char* path)
    {
        handle = std::fopen(path, "rb");
        assert(handle);
        std::fseek(handle, 0, SEEK_END);
        length = std::ftell(handle);
    }
    ~File() { std::fclose(handle); }
    uint32_t size() const override { return length; }
    std::size_t read_at(uint32_t offset, void* out, std::size_t bytes) override { return std::fseek(handle, offset, SEEK_SET) == 0 ? std::fread(out, 1, bytes, handle) : 0; }
};
int main(int argc, char** argv)
{
    assert(argc > 1);
    for (int n = 1; n < argc; n++)
    {
        File input(argv[n]);
        auto* track = music::make<music::Track>();
        auto* parser = music::make<music::TagReader>();
        assert(track && parser);
        std::strcpy(track->path, "/Music/test.mp3");
        assert(parser->read(input, *track));
        auto* decoder = music::make<music::Decoder>();
        assert(decoder && decoder->ready() && decoder->open(input, *track));
        unsigned frames = 0, peak = 0;
        music::PcmFrame frame;
        for (;;)
        {
            const auto result = decoder->next(frame);
            if (result == music::DecodeResult::End) break;
            assert(result == music::DecodeResult::Frame && frame.samples && frame.count <= 1152 && frame.rate >= 8000 && frame.rate <= 48000);
            for (unsigned i = 0; i < frame.count; i++) peak = std::max(peak, static_cast<unsigned>(std::abs(static_cast<int>(frame.samples[i]))));
            assert(++frames < 100000);
        }
        std::printf("Decoded %s: frames=%u peak=%u position=%lu duration=%lu\n", argv[n], frames, peak, static_cast<unsigned long>(decoder->position_ms()), static_cast<unsigned long>(track->duration_ms));
        std::fflush(stdout);
        assert(frames > 10 && peak > 100 && decoder->position_ms() > 250 && track->duration_ms > 250);
        assert(decoder->seek(track->duration_ms / 2));
        assert(decoder->next(frame) == music::DecodeResult::Frame);
        assert(decoder->position_ms() >= track->duration_ms / 2);
        std::printf("MP3 %s: frames=%u peak=%u duration=%lu ms workspace=%u bytes.\n", argv[n], frames, peak, static_cast<unsigned long>(track->duration_ms), static_cast<unsigned>(music::Decoder::workspace_bytes()));
        music::destroy(decoder);
        music::destroy(parser);
        music::destroy(track);
    }
}
