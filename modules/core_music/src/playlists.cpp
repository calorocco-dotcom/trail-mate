#include "music/playlists.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace music
{
namespace
{
constexpr const char* root = "/.trailmate/music/playlists";
bool valid_name(const char* name)
{
    const auto length = std::strlen(name);
    if (!length || length > 48) return false;
    bool visible = false;
    for (const uint8_t* p = reinterpret_cast<const uint8_t*>(name); *p; p++)
    {
        if (*p < 32 || *p == 127) return false;
        if (*p != ' ') visible = true;
    }
    return visible;
}
bool write_text(File& file, const char* text)
{
    const auto n = std::strlen(text);
    return file.write(text, n) == n;
}
bool playlist_id(const char* name, uint64_t& id)
{
    if (std::strlen(name) != 20 || std::strcmp(name + 16, ".m3u")) return false;
    for (unsigned i = 0; i < 16; i++)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    id = std::strtoull(name, nullptr, 16);
    return id != 0;
}
} // namespace
void Playlists::paths(uint64_t id)
{
    std::snprintf(path_, sizeof(path_), "%s/%016llx.m3u", root, static_cast<unsigned long long>(id));
    std::snprintf(temporary_, sizeof(temporary_), "%s.tmp", path_);
    std::snprintf(backup_, sizeof(backup_), "%s.bak", path_);
}
File* Playlists::open(uint64_t id)
{
    paths(id);
    if (!storage_.exists(path_) && storage_.exists(backup_)) storage_.rename(backup_, path_);
    auto* file = storage_.open(path_, FileMode::Read);
    reset_reader();
    if (file && file->size() > 4U * 1024U * 1024U)
    {
        storage_.close(file);
        return nullptr;
    }
    return file;
}
bool Playlists::read_line(File& file)
{
    std::size_t used = 0;
    bool too_long = false;
    while (cursor_ < file.size())
    {
        if (cache_at_ == UINT32_MAX || cursor_ < cache_at_ || cursor_ >= cache_at_ + cache_size_)
        {
            if (!catalog_.cooperate())
            {
                read_error_ = true;
                line_[0] = 0;
                return false;
            }
            cache_at_ = cursor_;
            cache_size_ = file.read_at(cursor_, cache_, std::min<uint32_t>(sizeof(cache_), file.size() - cursor_));
            if (!cache_size_)
            {
                read_error_ = true;
                line_[0] = 0;
                return false;
            }
        }
        const uint8_t c = cache_[cursor_++ - cache_at_];
        if (c == '\n') break;
        if (c == '\r') continue;
        if (used + 1 < sizeof(line_)) line_[used++] = c;
        else too_long = true;
    }
    line_[used] = 0;
    if (too_long) line_[0] = 0;
    return used != 0 || cursor_ < file.size();
}
bool Playlists::create(const char* name, uint64_t initial, uint64_t& out_id)
{
    out_id = 0;
    if (!storage_.ready() || !valid_name(name)) return false;
    if (!storage_.mkdir("/.trailmate") || !storage_.mkdir("/.trailmate/music") || !storage_.mkdir(root)) return false;
    for (unsigned n = 0; n < 32; n++)
    {
        const uint64_t id = path_id(name) ^ (uint64_t(storage_.session()) << 32) ^ (++sequence_ * 0x9e3779b97f4a7c15ULL);
        if (!id) continue;
        paths(id);
        if (storage_.exists(path_)) continue;
        auto* file = storage_.open(temporary_, FileMode::Write);
        if (!file) return false;
        bool ok = write_text(*file, "#EXTM3U\n#PLAYLIST:") && write_text(*file, name) && write_text(*file, "\n");
        if (initial)
        {
            ok = ok && catalog_.find(initial, track_) && write_text(*file, track_.path) && write_text(*file, "\n");
        }
        ok = file->flush() && ok;
        storage_.close(file);
        if (!ok || !storage_.rename(temporary_, path_))
        {
            storage_.remove(temporary_);
            return false;
        }
        out_id = id;
        return true;
    }
    return false;
}
bool Playlists::rewrite(uint64_t id, const char* name, uint64_t remove_track, const char* add_path)
{
    if (name && !valid_name(name)) return false;
    auto* original = open(id);
    if (!original) return false;
    auto* updated = storage_.open(temporary_, FileMode::Write);
    if (!updated)
    {
        storage_.close(original);
        return false;
    }
    bool ok = write_text(*updated, "#EXTM3U\n"), has_name = false, duplicate = false;
    unsigned tracks = 0;
    while (ok && read_line(*original))
    {
        if (!std::strncmp(line_, "#PLAYLIST:", 10))
        {
            ok = write_text(*updated, "#PLAYLIST:") && write_text(*updated, name ? name : line_ + 10) && write_text(*updated, "\n");
            has_name = true;
        }
        else if (valid_music_path(line_))
        {
            if (++tracks > 10000)
            {
                ok = false;
                break;
            }
            if (add_path && path_id(line_) == path_id(add_path)) duplicate = true;
            if (path_id(line_) != remove_track) ok = write_text(*updated, line_) && write_text(*updated, "\n");
        }
    }
    ok = ok && !read_error_;
    if (ok && !has_name) ok = write_text(*updated, "#PLAYLIST:") && write_text(*updated, name ? name : "Playlist") && write_text(*updated, "\n");
    if (ok && add_path && !duplicate) ok = tracks < 10000 && write_text(*updated, add_path) && write_text(*updated, "\n");
    ok = updated->flush() && ok;
    storage_.close(updated);
    storage_.close(original);
    if (!ok)
    {
        storage_.remove(temporary_);
        return false;
    }
    if (storage_.exists(backup_) && !storage_.remove(backup_))
    {
        storage_.remove(temporary_);
        return false;
    }
    if (!storage_.rename(path_, backup_))
    {
        storage_.remove(temporary_);
        return false;
    }
    if (!storage_.rename(temporary_, path_))
    {
        storage_.rename(backup_, path_);
        return false;
    }
    storage_.remove(backup_);
    return true;
}
bool Playlists::add(uint64_t playlist, uint64_t song)
{
    if (!catalog_.find(song, track_)) return false;
    // track_.path belongs to this object and remains stable while streaming the playlist rewrite.
    return rewrite(playlist, nullptr, 0, track_.path);
}
bool Playlists::remove(uint64_t playlist, uint64_t song) { return rewrite(playlist, nullptr, song, nullptr); }
bool Playlists::rename(uint64_t playlist, const char* name) { return rewrite(playlist, name, 0, nullptr); }
bool Playlists::erase(uint64_t playlist)
{
    paths(playlist);
    if (storage_.exists(backup_) && !storage_.remove(backup_)) return false;
    if (storage_.exists(temporary_)) storage_.remove(temporary_);
    return storage_.remove(path_);
}
uint32_t Playlists::count(uint64_t playlist)
{
    auto* file = open(playlist);
    if (!file) return 0;
    uint32_t count = 0;
    while (read_line(*file))
        if (valid_music_path(line_) && ++count == 10000) break;
    storage_.close(file);
    return read_error_ ? 0 : count;
}
bool Playlists::track(uint64_t playlist, uint32_t index, Track& out)
{
    if (index >= 10000) return false;
    auto* file = open(playlist);
    if (!file) return false;
    bool found = false;
    while (read_line(*file))
        if (valid_music_path(line_) && index-- == 0)
        {
            found = catalog_.find(path_id(line_), out);
            break;
        }
    storage_.close(file);
    return found;
}
bool Playlists::browse(Snapshot& out, uint8_t rows)
{
    out.row_count = 0;
    out.total = 0;
    rows = std::min<uint8_t>(rows, kPageRows);
    if (out.browse == Browse::PlaylistTracks)
    {
        auto* file = open(out.playlist_id);
        if (!file) return false;
        while (read_line(*file))
            if (valid_music_path(line_))
            {
                if (out.total == 10000) break;
                const auto at = out.total++;
                if (at < out.offset || out.row_count >= rows) continue;
                auto& row = out.rows[out.row_count++];
                row = {};
                row.id = path_id(line_);
                if (catalog_.find(row.id, track_))
                {
                    copy_text(row.title, sizeof(row.title), track_.title);
                    copy_text(row.subtitle, sizeof(row.subtitle), track_.artist);
                }
                else
                {
                    const auto* name = std::strrchr(line_, '/');
                    copy_text(row.title, sizeof(row.title), name ? name + 1 : line_);
                    copy_text(row.subtitle, sizeof(row.subtitle), "Missing from library");
                }
                if (out.total == 10000) break;
            }
        storage_.close(file);
        return !read_error_;
    }
    uint64_t cursor = 0;
    for (;;)
    {
        if (!catalog_.cooperate()) return false;
        const auto result = storage_.next(root, cursor, entry_);
        if (result == StorageResult::End || result == StorageResult::Missing) return true;
        if (result != StorageResult::Ok) return false;
        uint64_t id = 0;
        if (entry_.directory || !playlist_id(entry_.name, id)) continue;
        const uint32_t at = out.total++;
        if (at < out.offset || out.row_count >= rows) continue;
        auto& row = out.rows[out.row_count++];
        row = {};
        row.id = id;
        copy_text(row.title, sizeof(row.title), "Playlist");
        auto* file = open(id);
        if (!file) return false;
        while (read_line(*file))
        {
            if (!std::strncmp(line_, "#PLAYLIST:", 10)) copy_text(row.title, sizeof(row.title), line_ + 10);
            else if (valid_music_path(line_)) ++row.count;
            if (row.count == 10000) break;
        }
        storage_.close(file);
        if (read_error_) return false;
        std::snprintf(row.subtitle, sizeof(row.subtitle), "%lu songs", static_cast<unsigned long>(row.count));
    }
}
} // namespace music
