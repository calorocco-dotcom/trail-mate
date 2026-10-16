#pragma once
#include "music/catalog.h"
namespace music
{
class Playlists
{
    Storage& storage_;
    Catalog& catalog_;
    uint8_t cache_[512]{};
    char line_[320]{}, path_[128]{}, temporary_[144]{}, backup_[144]{};
    Track track_{};
    uint32_t cursor_ = 0, cache_at_ = UINT32_MAX, cache_size_ = 0, sequence_ = 0;
    bool read_error_ = false;
    Entry entry_{};
    bool read_line(File& file);
    void reset_reader()
    {
        cursor_ = 0;
        cache_at_ = UINT32_MAX;
        cache_size_ = 0;
        read_error_ = false;
    }
    void paths(uint64_t id);
    File* open(uint64_t id);
    bool rewrite(uint64_t id, const char* name, uint64_t remove_track, const char* add_path);

  public:
    Playlists(Storage& storage, Catalog& catalog) : storage_(storage), catalog_(catalog) {}
    bool create(const char* name, uint64_t initial_track, uint64_t& out_id);
    bool add(uint64_t playlist, uint64_t song);
    bool remove(uint64_t playlist, uint64_t song);
    bool rename(uint64_t playlist, const char* name);
    bool erase(uint64_t playlist);
    bool browse(Snapshot& out, uint8_t rows);
    uint32_t count(uint64_t playlist);
    bool track(uint64_t playlist, uint32_t index, Track& out);
};
} // namespace music
