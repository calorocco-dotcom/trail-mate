#include "music/lyrics.h"
#include <cassert>
#include <cstring>
#include <string>
struct Text : music::Reader
{
    std::string content;
    uint32_t size() const override { return content.size(); }
    std::size_t read_at(uint32_t at, void* out, std::size_t count) override
    {
        count = std::min(count, content.size() - at);
        std::memcpy(out, content.data() + at, count);
        return count;
    }
};
int main()
{
    Text file;
    file.content = "[ar:Aster]\n[offset:200]\n[00:01.00]First line\n[00:02.50][00:04.00]Second line\n[00:03.00]Third line\n";
    auto* lyrics = music::make<music::Lyrics>();
    auto* snapshot = music::make<music::Snapshot>();
    assert(lyrics && snapshot && lyrics->open(file, true));
    lyrics->update(1210, *snapshot);
    assert(!std::strcmp(snapshot->lyric_current, "First line"));
    lyrics->update(2800, *snapshot);
    assert(!std::strcmp(snapshot->lyric_current, "Second line") && !std::strcmp(snapshot->lyric_next, "Third line"));
    lyrics->update(4400, *snapshot);
    assert(!std::strcmp(snapshot->lyric_current, "Second line") && !snapshot->lyric_next[0]);
    assert(!lyrics->update(4410, *snapshot));
    assert(lyrics->open(file, true));
    lyrics->update(0, *snapshot);
    assert(!snapshot->lyric_current[0] && !snapshot->lyric_previous[0] && !std::strcmp(snapshot->lyric_next, "First line"));
    file.content = "Plain one\nPlain two\nPlain three";
    assert(lyrics->open(file, false));
    lyrics->update(9000, *snapshot);
    assert(!snapshot->synchronized_lyrics && !std::strcmp(snapshot->lyric_current, "Plain one"));
    lyrics->scroll(1);
    lyrics->update(9000, *snapshot);
    assert(!std::strcmp(snapshot->lyric_current, "Plain two"));
    file.content = "\xff\xfeP\0l\0a\0i\0n\0"; // explicit-size UTF16 fixture follows.
    const unsigned char unicode[] = {0xff, 0xfe, 0x2d, 0x4e, 0x87, 0x65, 10, 0, 0x4c, 0x6b, 0xcd, 0x8b};
    file.content.assign(reinterpret_cast<const char*>(unicode), sizeof(unicode));
    assert(lyrics->open(file, false));
    lyrics->update(0, *snapshot);
    assert(!std::strcmp(snapshot->lyric_current, "\xe4\xb8\xad\xe6\x96\x87"));
    lyrics->clear();
    lyrics->update(0, *snapshot);
    assert(!snapshot->has_lyrics);
    music::destroy(lyrics);
    music::destroy(snapshot);
}
