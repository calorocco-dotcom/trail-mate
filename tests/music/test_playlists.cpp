#include "memory_storage.h"
#include "music/playlists.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream mp3(argv[1], std::ios::binary);
    assert(mp3);
    MemoryStorage storage;
    const MemoryStorage::Bytes sound(std::istreambuf_iterator<char>(mp3), {});
    storage.files["/Music/first.mp3"] = sound;
    storage.files["/Music/second.mp3"] = sound;
    auto* catalog = music::make<music::Catalog>(storage);
    assert(catalog->begin_scan());
    while (catalog->scan_step() == music::StorageResult::Ok)
    {
    }
    assert(catalog->count() == 2);
    auto* lists = music::make<music::Playlists>(storage, *catalog);
    auto* out = music::make<music::Snapshot>();
    auto* track = music::make<music::Track>();
    uint64_t id = 0;
    const auto first = music::path_id("/Music/first.mp3"), second = music::path_id("/Music/second.mp3");
    assert(lists->create("Evening trail", first, id) && id && lists->count(id) == 1);
    assert(lists->add(id, first) && lists->count(id) == 1);
    assert(lists->add(id, second) && lists->count(id) == 2);
    assert(lists->track(id, 1, *track) && track->id == second);
    assert(lists->rename(id, "Ridge walk"));
    out->browse = music::Browse::Playlists;
    assert(lists->browse(*out, 8) && out->total == 1 && !std::strcmp(out->rows[0].title, "Ridge walk"));
    assert(lists->remove(id, first) && lists->count(id) == 1);
    out->browse = music::Browse::PlaylistTracks;
    out->playlist_id = id;
    assert(lists->browse(*out, 8) && out->total == 1 && out->rows[0].id == second);
    storage.fail_write = true;
    assert(!lists->add(id, first));
    storage.fail_write = false;
    assert(lists->count(id) == 1);
    unsigned yields = 0;
    catalog->set_yield([](void* context)
                       { ++*static_cast<unsigned*>(context); return false; },
                       &yields);
    assert(!lists->add(id, first) && yields > 0);
    catalog->set_yield(nullptr, nullptr);
    assert(lists->count(id) == 1); // Cancellation cannot commit a truncated rewrite.
    uint64_t invalid = 0;
    assert(!lists->create("Name\nInjected", 0, invalid));
    assert(!lists->create("   ", 0, invalid));
    char path[128], backup[144];
    std::snprintf(path, sizeof(path), "/.trailmate/music/playlists/%016llx.m3u", static_cast<unsigned long long>(id));
    std::snprintf(backup, sizeof(backup), "%s.bak", path);
    assert(storage.rename(path, backup));
    assert(lists->count(id) == 1 && storage.exists(path));
    assert(lists->erase(id) && !storage.exists(path));
    assert(storage.files.count("/Music/first.mp3") && storage.files.count("/Music/second.mp3"));
    music::destroy(lists);
    music::destroy(catalog);
    music::destroy(out);
    music::destroy(track);
    std::puts("Persistent M3U8 creation/add/dedup/remove/rename/delete, failed write rollback and backup recovery passed.");
}
