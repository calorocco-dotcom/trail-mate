#include "memory_storage.h"
#include "music/catalog.h"
#include <cassert>
#include <cstdio>
using Bytes = MemoryStorage::Bytes;
void frame(Bytes& frames, const char* id, const std::string& value)
{
    frames.insert(frames.end(), id, id + 4);
    const auto length = value.size() + 1;
    frames.insert(frames.end(), {uint8_t(length >> 24), uint8_t(length >> 16), uint8_t(length >> 8), uint8_t(length), 0, 0, 3});
    frames.insert(frames.end(), value.begin(), value.end());
}
Bytes song(unsigned index)
{
    Bytes frames;
    frame(frames, "TIT2", "Song " + std::to_string(index));
    frame(frames, "TPE1", "Artist " + std::to_string(index % 4));
    frame(frames, "TALB", "Album " + std::to_string(index % 8));
    const auto size = frames.size();
    Bytes out{'I', 'D', '3', 3, 0, 0, uint8_t((size >> 21) & 127), uint8_t((size >> 14) & 127), uint8_t((size >> 7) & 127), uint8_t(size & 127)};
    out.insert(out.end(), frames.begin(), frames.end());
    out.insert(out.end(), 128, 0x55);
    return out;
}
void scan(music::Catalog& catalog)
{
    assert(catalog.begin_scan());
    unsigned steps = 0;
    for (;;)
    {
        const auto result = catalog.scan_step();
        if (result == music::StorageResult::End) break;
        assert(result == music::StorageResult::Ok && ++steps < 2000);
    }
}
int main()
{
    MemoryStorage storage;
    for (unsigned i = 0; i < 160; i++)
    {
        char path[80];
        std::snprintf(path, sizeof(path), "/Music/song-%03u.mp3", i);
        storage.files[path] = song(i);
    }
    auto* catalog = music::make<music::Catalog>(storage);
    auto* out = music::make<music::Snapshot>();
    auto* track = music::make<music::Track>();
    assert(catalog && out && track);
    assert(!catalog->load());
    scan(*catalog);
    assert(catalog->count() == 160);
    out->browse = music::Browse::Songs;
    out->offset = 80;
    storage.read_bytes = 0;
    storage.opens = 0;
    assert(catalog->browse(*out, 8) && out->row_count == 8 && out->total == 160);
    assert(storage.read_bytes == 8 * 580 && storage.opens == 1);
    assert(!std::strcmp(out->rows[0].title, "Song 80"));
    const auto id = out->rows[3].id;
    assert(catalog->find(id, *track) && !std::strcmp(track->title, "Song 83"));
    out->browse = music::Browse::Artists;
    out->offset = 0;
    assert(catalog->browse(*out, 8) && out->total == 4 && out->rows[0].count == 40);
    assert(catalog->queue_count(music::Browse::ArtistTracks, "Artist 0") == 40);
    assert(catalog->queue_track(music::Browse::ArtistTracks, "Artist 0", 3, *track) && !std::strcmp(track->title, "Song 12"));
    // An unfinished inactive lane is never published after a reboot.
    assert(catalog->begin_scan());
    assert(catalog->scan_step() == music::StorageResult::Ok);
    catalog->abort_scan();
    music::destroy(catalog);
    catalog = music::make<music::Catalog>(storage);
    assert(catalog->load() && catalog->count() == 160);
    storage.files["/Music/new.mp3"] = song(999);
    scan(*catalog);
    assert(catalog->count() == 161);
    // The newest manifest is corrupt: recover the previous complete lane.
    storage.files["/.trailmate/music/manifest-1.idx"][20] ^= 1;
    music::destroy(catalog);
    catalog = music::make<music::Catalog>(storage);
    assert(catalog->load() && catalog->count() == 160);
    storage.files["/.trailmate/music/songs-0.idx"][70] ^= 1;
    assert(!catalog->track(0, *track));
    ++storage.media_session;
    assert(!catalog->track(80, *track));
    assert(!music::valid_music_path("/Music/../secret.mp3") && !music::valid_music_path("/Music/a\nb.mp3"));
    music::destroy(catalog);
    music::destroy(out);
    music::destroy(track);
    std::puts("Paged SD catalog, fixed records, stable ids, category counts, incomplete scan recovery, CRC and media session checks passed.");
}
