#include "music/cover_decoder.h"
#include "music/id3.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace
{
using Bytes = std::vector<uint8_t>;
struct MemoryReader : music::Reader
{
    Bytes bytes;
    unsigned read_bytes = 0;
    uint32_t size() const override { return bytes.size(); }
    std::size_t read_at(uint32_t at, void* output, std::size_t size) override
    {
        assert(at <= bytes.size());
        size = std::min(size, bytes.size() - at);
        std::memcpy(output, bytes.data() + at, size);
        read_bytes += size;
        return size;
    }
};
void append(Bytes& out, const Bytes& data) { out.insert(out.end(), data.begin(), data.end()); }
Bytes text(const char* value, uint8_t encoding = 3)
{
    Bytes out{encoding};
    out.insert(out.end(), value, value + std::strlen(value));
    return out;
}
Bytes length(uint32_t value, bool sync)
{
    return sync ? Bytes{uint8_t((value >> 21) & 127), uint8_t((value >> 14) & 127), uint8_t((value >> 7) & 127), uint8_t(value & 127)} : Bytes{uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
}
void frame(Bytes& out, const char* id, const Bytes& data, int version = 3, uint8_t flags = 0)
{
    out.insert(out.end(), id, id + std::strlen(id));
    auto size = length(data.size(), version == 4);
    if (version == 2) out.insert(out.end(), size.begin() + 1, size.end());
    else
    {
        append(out, size);
        out.push_back(0);
        out.push_back(flags);
    }
    append(out, data);
}
Bytes tag(const Bytes& payload, int version = 3, bool footer = false)
{
    Bytes out{'I', 'D', '3', uint8_t(version), 0, uint8_t(footer ? 16 : 0)};
    append(out, length(payload.size(), true));
    append(out, payload);
    if (footer)
    {
        Bytes foot{'3', 'D', 'I', 4, 0, 16};
        append(foot, length(payload.size(), true));
        append(out, foot);
    }
    out.insert(out.end(), 512, 0x55);
    return out;
}
Bytes file(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return Bytes(std::istreambuf_iterator<char>(input), {});
}
Bytes picture(const Bytes& image, uint8_t type = 3, const char* mime = "image/jpeg")
{
    Bytes out{0};
    out.insert(out.end(), mime, mime + std::strlen(mime) + 1);
    out.push_back(type);
    out.push_back(0);
    append(out, image);
    return out;
}
} // namespace
int main(int argc, char** argv)
{
    assert(argc == 2);
    auto* parser = music::make<music::TagReader>();
    auto* track = music::make<music::Track>();
    auto* page = music::make<music::MetadataPage>();
    assert(parser && track && page);
    track->id = 42;
    std::strcpy(track->path, "/Music/fallback.mp3");
    Bytes frames;
    frame(frames, "TIT2", text("After the rain"));
    frame(frames, "TPE1", text("Aster"));
    frame(frames, "TALB", text("Small horizons"));
    frame(frames, "TPE2", text("Album artist"));
    frame(frames, "TCOM", text("Composer"));
    frame(frames, "TRCK", text("1/12"));
    frame(frames, "TPOS", text("1/1"));
    frame(frames, "TCOP", text("Original fixture"));
    frame(frames, "TENC", text("Test encoder"));
    frame(frames, "APIC", picture(file(std::string(argv[1]) + "/cover.jpg"), 4));
    frame(frames, "APIC", picture(file(std::string(argv[1]) + "/cover.jpg"), 3));
    MemoryReader input;
    input.bytes = tag(frames);
    assert(parser->read(input, *track, page));
    assert(!std::strcmp(track->title, "After the rain") && !std::strcmp(track->album, "Small horizons"));
    assert(track->picture_type == 3 && track->picture_format == music::PictureFormat::Jpeg && page->total == 12 && page->count == 6);
    assert(input.read_bytes < 4096);
    assert(parser->read(input, *track, page, 6));
    assert(!std::strcmp(page->fields[0].id, "TPOS"));
    music::CoverResult result;
    auto* art = music::decode_cover(input, *track, 96, result);
    assert(art && result == music::CoverResult::Ready && art->width == 96 && art->height == 96);
    assert(art->pixels[10 * 96 + 10] != art->pixels[90 * 96 + 10]);
    art->retain();
    art->drop();
    assert(art->pixels[0]);
    art->drop();
    unsigned pumps = 0;
    art = music::decode_cover(
        input, *track, 96, result, [](void* context)
        {
        ++*static_cast<unsigned*>(context);
        return false; },
        &pumps);
    assert(!art && result == music::CoverResult::Cancelled && pumps == 1);
    // Cancellation must release the decoder and thumbnail so a retry succeeds.
    art = music::decode_cover(input, *track, 96, result);
    assert(art && result == music::CoverResult::Ready);
    art->drop();
    for (const auto* name : {"cover.png", "landscape.jpg", "progressive.jpg"})
    {
        frames.clear();
        frame(frames, "APIC", picture(file(std::string(argv[1]) + "/" + name), 3, std::strstr(name, "png") ? "image/png" : "image/jpeg"));
        input.bytes = tag(frames);
        assert(parser->read(input, *track, page));
        art = music::decode_cover(input, *track, 96, result);
        if (std::strstr(name, "progressive"))
        {
            assert(!art && result == music::CoverResult::Unsupported);
            continue;
        }
        assert(art && result == music::CoverResult::Ready && art->width == 96);
        if (std::strstr(name, "landscape")) assert(art->height == 48);
        art->drop();
    }
    for (const auto* name : {"color-bars.jpg", "color-bars.png"})
    {
        frames.clear();
        frame(frames, "APIC", picture(file(std::string(argv[1]) + "/" + name), 3, std::strstr(name, "png") ? "image/png" : "image/jpeg"));
        input.bytes = tag(frames);
        assert(parser->read(input, *track, page));
        art = music::decode_cover(input, *track, 64, result);
        assert(art && result == music::CoverResult::Ready && art->width == 64 && art->height == 32);
        const uint16_t expected[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
        for (unsigned index = 0; index < 4; ++index)
        {
            const auto pixel = art->pixels[16 * art->width + index * 16 + 8];
            // JPEG quantization may lose a few low bits; channels must retain
            // the same ordering as the lossless PNG and the RGB565 display.
            assert(std::abs(int(pixel >> 11) - int(expected[index] >> 11)) <= 1);
            assert(std::abs(int((pixel >> 5) & 63) - int((expected[index] >> 5) & 63)) <= 1);
            assert(std::abs(int(pixel & 31) - int(expected[index] & 31)) <= 1);
        }
        art->drop();
    }
    frames.clear();
    frame(frames, "TIT2", Bytes{1, 0xff, 0xfe, 0x2d, 0x4e, 0x87, 0x65, 0, 0});
    input.bytes = tag(frames, 3);
    assert(parser->read(input, *track, page));
    assert(!std::strcmp(track->title, "\xe4\xb8\xad\xe6\x96\x87"));
    frames.clear();
    Bytes website{1, 0xfe, 0xff, 0x4e, 0x2d, 0x65, 0x87, 0, 0};
    const char* url = "https://example.test/music";
    website.insert(website.end(), url, url + std::strlen(url));
    frame(frames, "WXXX", website);
    frame(frames, "USLT", Bytes{1, 'e', 'n', 'g', 0xfe, 0xff, 0, 0, 0x4e, 0x2d, 0x65, 0x87});
    input.bytes = tag(frames);
    assert(parser->read(input, *track, page));
    assert(!std::strcmp(page->fields[0].value, "\xe4\xb8\xad\xe6\x96\x87 / https://example.test/music"));
    assert(track->lyric_encoding == 2);
    char lyrics[32];
    parser->read_text(input, track->lyric_offset, track->lyric_offset + track->lyric_size, track->lyric_encoding, lyrics, sizeof(lyrics));
    assert(!std::strcmp(lyrics, "\xe4\xb8\xad\xe6\x96\x87"));
    frames.clear();
    frame(frames, "TIT2", text("Footer song"), 4);
    input.bytes = tag(frames, 4, true);
    assert(parser->read(input, *track, page) && track->audio_offset == frames.size() + 20);
    assert(!std::strcmp(track->title, "Footer song"));
    frames.clear();
    frame(frames, "TT2", text("Legacy", 0), 2);
    auto pic = Bytes{0, 'J', 'P', 'G', 3, 0};
    append(pic, file(std::string(argv[1]) + "/cover.jpg"));
    frame(frames, "PIC", pic, 2);
    input.bytes = tag(frames, 2);
    assert(parser->read(input, *track, page) && !std::strcmp(track->title, "Legacy") && track->picture_size);
    // 1MB artwork remains on SD during metadata scan, then is capped before decoding PNG.
    frames.clear();
    frame(frames, "APIC", picture(Bytes(1024 * 1024, 0), 3, "image/png"));
    input.bytes = tag(frames);
    input.read_bytes = 0;
    assert(parser->read(input, *track, page) && input.read_bytes < 4096);
    assert(!music::decode_cover(input, *track, 96, result) && result == music::CoverResult::Invalid);
    track->picture_size = 3 * 1024 * 1024;
    assert(!music::decode_cover(input, *track, 96, result) && result == music::CoverResult::TooLarge);
    frames.clear();
    frame(frames, "TIT2", text("Broken"));
    input.bytes = tag(frames);
    input.bytes[14] = 0x7f;
    assert(!parser->read(input, *track, page));
    input.bytes = Bytes{'I', 'D', '3', 4, 0, 0, 0x80, 0, 0, 0};
    assert(!parser->read(input, *track, page));
    std::printf("ID3 2.2/2.3/2.4, UTF16, metadata paging, APIC front-cover selection, JPEG/PNG decoding, aspect ratio, malformed bounds and bounded reads passed.\n");
    music::destroy(parser);
    music::destroy(track);
    music::destroy(page);
}
