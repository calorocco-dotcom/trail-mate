#include "music/catalog.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace music
{
namespace
{
constexpr const char* kRoot = "/.trailmate/music";
constexpr const char* types[] = {"artists", "albums", "folders"};
constexpr uint32_t kRecordSize = 580, kManifestSize = 1088, kMaxTracks = 1000000;
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void put32(uint8_t* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) p[i] = value >> (i * 8);
}
uint64_t get64(const uint8_t* p) { return get32(p) | (uint64_t(get32(p + 4)) << 32); }
void put64(uint8_t* p, uint64_t value)
{
    put32(p, value);
    put32(p + 4, value >> 32);
}
uint32_t crc(const uint8_t* p, std::size_t size)
{
    uint32_t value = ~0U;
    while (size--)
    {
        value ^= *p++;
        for (unsigned n = 0; n < 8; n++) value = (value >> 1) ^ (0xedb88320U & -(value & 1));
    }
    return ~value;
}
const char* group_name(const Track& track, unsigned kind, char* folder, std::size_t capacity)
{
    if (kind == 0) return track.artist[0] ? track.artist : "Unknown artist";
    if (kind == 1) return track.album[0] ? track.album : "Unknown album";
    copy_text(folder, capacity, track.path + 7);
    auto* slash = std::strrchr(folder, '/');
    if (slash) *slash = 0;
    else copy_text(folder, capacity, "Music");
    return folder;
}
bool is_mp3(const char* name)
{
    const auto size = std::strlen(name);
    if (size < 4) return false;
    const char* ext = name + size - 4;
    return ext[0] == '.' && (ext[1] == 'm' || ext[1] == 'M') && (ext[2] == 'p' || ext[2] == 'P') && ext[3] == '3';
}
bool same_category(const Track& track, Browse kind, const char* category, char* folder, std::size_t capacity)
{
    return kind == Browse::Songs || path_id(category) == path_id(group_name(track, kind == Browse::ArtistTracks ? 0 : kind == Browse::AlbumTracks ? 1
                                                                                                                                                  : 2,
                                                                            folder, capacity));
}
} // namespace
struct Catalog::Scan
{
    struct Directory
    {
        char path[256]{};
        uint64_t cursor = 0;
    };
    struct Group
    {
        uint64_t id = 0;
        uint32_t first = 0, count = 0;
    };
    Directory directories[8]{};
    Group groups[3][512]{};
    uint16_t group_count[3]{};
    uint32_t heads[256]{}, count = 0, generation = 0;
    uint8_t depth = 0, lane = 0;
    File* output = nullptr;
    TagReader tags;
    bool truncated = false;
    char folder[256]{};
};
Catalog::~Catalog() { abort_scan(); }
void Catalog::index_path(unsigned lane, const char* type, char* out, std::size_t capacity) const
{
    std::snprintf(out, capacity, "%s/%s-%u.idx", kRoot, type, lane);
}
bool Catalog::load_lane(uint8_t lane, bool apply)
{
    index_path(lane, "manifest", path_, sizeof(path_));
    auto* file = storage_.open(path_, FileMode::Read);
    if (!file) return false;
    const bool read = file->size() == kManifestSize && file->read_at(0, manifest_, kManifestSize) == kManifestSize;
    storage_.close(file);
    if (!read || std::memcmp(manifest_, "TMUM", 4) || get32(manifest_ + 4) != 1 || get32(manifest_ + 1084) != crc(manifest_, 1084)) return false;
    const auto generation = get32(manifest_ + 8), count = get32(manifest_ + 12);
    if (!generation || count > kMaxTracks) return false;
    for (unsigned kind = 0; kind < 4; kind++)
    {
        const uint32_t records = kind ? get32(manifest_ + 12 + kind * 4) : count;
        if (kind && records > 512) return false;
        index_path(lane, kind ? types[kind - 1] : "songs", path_, sizeof(path_));
        file = storage_.open(path_, FileMode::Read);
        if (!file) return false;
        const bool valid = file->size() == uint64_t(records) * (kind ? 16 : kRecordSize);
        storage_.close(file);
        if (!valid) return false;
    }
    for (unsigned i = 0; i < 256; i++)
        if (get32(manifest_ + 32 + i * 4) > count) return false;
    if (apply)
    {
        generation_ = generation;
        count_ = count;
        lane_ = lane;
        session_ = storage_.session();
        truncated_ = manifest_[28] != 0;
        for (unsigned i = 0; i < 3; i++) groups_[i] = get32(manifest_ + 16 + i * 4);
        for (unsigned i = 0; i < 256; i++)
        {
            heads_[i] = get32(manifest_ + 32 + i * 4);
        }
    }
    return true;
}
bool Catalog::load()
{
    if (!storage_.ready()) return false;
    generation_ = 0;
    count_ = 0;
    const bool first = load_lane(0, true);
    const auto first_generation = generation_;
    if (load_lane(1, false) && (!first || static_cast<int32_t>(get32(manifest_ + 8) - first_generation) > 0)) return load_lane(1, true);
    if (first) return load_lane(0, true);
    return false;
}
bool Catalog::read_record(uint32_t index, Track& out, uint32_t* link)
{
    if (index >= count_ || storage_.session() != session_) return false;
    index_path(lane_, "songs", path_, sizeof(path_));
    auto* file = storage_.open(path_, FileMode::Read);
    if (!file) return false;
    const bool read = read_record_from(*file, index, out, link);
    storage_.close(file);
    return read;
}
bool Catalog::read_record_from(File& file, uint32_t index, Track& out, uint32_t* link)
{
    if (index >= count_ || storage_.session() != session_) return false;
    const bool read = file.read_at(index * kRecordSize, record_, sizeof(record_)) == sizeof(record_);
    if (!read || get32(record_ + 576) != crc(record_, 576)) return false;
    if (get32(record_ + 36) > index) return false; // Hash links may only point to earlier records.
    // Stored strings must be NUL-terminated before any C-string operation.
    for (const auto end : {319, 415, 479, 543, 575})
        if (record_[end]) return false;
    if (!valid_music_path(reinterpret_cast<const char*>(record_ + 64))) return false;
    out = {};
    out.id = get64(record_);
    out.duration_ms = get32(record_ + 8);
    out.audio_offset = get32(record_ + 12);
    out.lyric_offset = get32(record_ + 16);
    out.lyric_size = get32(record_ + 20);
    out.lyric_encoding = record_[24];
    out.estimated_duration = record_[25] != 0;
    out.picture_format = record_[26] <= 2 ? static_cast<PictureFormat>(record_[26]) : PictureFormat::None;
    out.picture_type = record_[27];
    out.picture_offset = get32(record_ + 28);
    out.picture_size = get32(record_ + 32);
    if (link) *link = get32(record_ + 36);
    std::memcpy(out.path, record_ + 64, 256);
    std::memcpy(out.title, record_ + 320, 96);
    std::memcpy(out.artist, record_ + 416, 64);
    std::memcpy(out.album, record_ + 480, 64);
    std::memcpy(out.genre, record_ + 544, 32);
    return out.id == path_id(out.path);
}
bool Catalog::write_record(File& file, const Track& track, uint32_t link)
{
    std::memset(record_, 0, sizeof(record_));
    put64(record_, track.id);
    put32(record_ + 8, track.duration_ms);
    put32(record_ + 12, track.audio_offset);
    put32(record_ + 16, track.lyric_offset);
    put32(record_ + 20, track.lyric_size);
    record_[24] = track.lyric_encoding;
    record_[25] = track.estimated_duration;
    record_[26] = static_cast<uint8_t>(track.picture_format);
    record_[27] = track.picture_type;
    put32(record_ + 28, track.picture_offset);
    put32(record_ + 32, track.picture_size);
    put32(record_ + 36, link);
    copy_text(reinterpret_cast<char*>(record_ + 64), 256, track.path);
    copy_text(reinterpret_cast<char*>(record_ + 320), 96, track.title);
    copy_text(reinterpret_cast<char*>(record_ + 416), 64, track.artist);
    copy_text(reinterpret_cast<char*>(record_ + 480), 64, track.album);
    copy_text(reinterpret_cast<char*>(record_ + 544), 32, track.genre);
    put32(record_ + 576, crc(record_, 576));
    return file.write(record_, sizeof(record_)) == sizeof(record_);
}
bool Catalog::find(uint64_t id, Track& out, uint32_t* index)
{
    uint32_t next = heads_[id & 255];
    for (uint32_t seen = 0; next && seen < count_; seen++)
    {
        if (seen && (seen % 8 == 0) && !cooperate()) return false;
        const uint32_t at = next - 1;
        if (!read_record(at, out, &next)) return false;
        if (out.id == id)
        {
            if (index) *index = at;
            return true;
        }
    }
    return false;
}
bool Catalog::begin_scan()
{
    if (scan_ || !storage_.ready()) return false;
    if (!storage_.mkdir("/.trailmate") || !storage_.mkdir(kRoot)) return false;
    scan_ = make<Scan>();
    if (!scan_) return false;
    scan_->lane = generation_ ? 1 - lane_ : 0;
    scan_->generation = generation_ + 1;
    if (!scan_->generation) scan_->generation = 1;
    session_ = storage_.session();
    copy_text(scan_->directories[0].path, 256, "/Music");
    // An old inactive manifest must not reference files that are about to be truncated.
    index_path(scan_->lane, "manifest", path_, sizeof(path_));
    if (storage_.exists(path_) && !storage_.remove(path_))
    {
        abort_scan();
        return false;
    }
    index_path(scan_->lane, "songs", path_, sizeof(path_));
    scan_->output = storage_.open(path_, FileMode::Write);
    if (!scan_->output)
    {
        abort_scan();
        return false;
    }
    return true;
}
void Catalog::abort_scan()
{
    if (scan_)
    {
        storage_.close(scan_->output);
        destroy(scan_);
    }
}
bool Catalog::truncated() const { return scan_ ? scan_->truncated : truncated_; }
StorageResult Catalog::scan_step()
{
    if (!scan_) return StorageResult::Error;
    if (!storage_.ready() || storage_.session() != session_)
    {
        abort_scan();
        return StorageResult::Unavailable;
    }
    auto& scan = *scan_;
    auto& dir = scan.directories[scan.depth];
    const uint64_t previous = dir.cursor;
    const auto result = storage_.next(dir.path, dir.cursor, entry_);
    if (result == StorageResult::Busy) return result;
    if (result == StorageResult::End || result == StorageResult::Missing)
    {
        if (scan.depth)
        {
            --scan.depth;
            return StorageResult::Ok;
        }
        if (!scan.output->flush())
        {
            abort_scan();
            return StorageResult::Error;
        }
        storage_.close(scan.output);
        for (unsigned kind = 0; kind < 3; kind++)
        {
            index_path(scan.lane, types[kind], path_, sizeof(path_));
            auto* group_file = storage_.open(path_, FileMode::Write);
            if (!group_file)
            {
                abort_scan();
                return StorageResult::Error;
            }
            bool ok = true;
            for (unsigned n = 0; n < scan.group_count[kind]; n++)
            {
                const auto& group = scan.groups[kind][n];
                put64(record_, group.id);
                put32(record_ + 8, group.first);
                put32(record_ + 12, group.count);
                if (group_file->write(record_, 16) != 16)
                {
                    ok = false;
                    break;
                }
            }
            ok = group_file->flush() && ok;
            storage_.close(group_file);
            if (!ok)
            {
                abort_scan();
                return StorageResult::Error;
            }
        }
        std::memset(manifest_, 0, sizeof(manifest_));
        std::memcpy(manifest_, "TMUM", 4);
        put32(manifest_ + 4, 1);
        put32(manifest_ + 8, scan.generation);
        put32(manifest_ + 12, scan.count);
        manifest_[28] = scan.truncated;
        for (unsigned i = 0; i < 3; i++) put32(manifest_ + 16 + i * 4, scan.group_count[i]);
        for (unsigned i = 0; i < 256; i++) put32(manifest_ + 32 + i * 4, scan.heads[i]);
        put32(manifest_ + 1084, crc(manifest_, 1084));
        index_path(scan.lane, "manifest", path_, sizeof(path_));
        auto* manifest = storage_.open(path_, FileMode::Write);
        bool ok = manifest && manifest->write(manifest_, sizeof(manifest_)) == sizeof(manifest_) && manifest->flush();
        storage_.close(manifest);
        const auto lane = scan.lane;
        abort_scan();
        if (!ok || !load_lane(lane, true)) return StorageResult::Error;
        return StorageResult::End;
    }
    if (result != StorageResult::Ok)
    {
        abort_scan();
        return result;
    }
    if (!entry_.name[0] || entry_.name[0] == '.') return StorageResult::Ok;
    const int length = std::snprintf(path_, sizeof(path_), "%s/%s", dir.path, entry_.name);
    if (length < 0 || length >= 256 || !valid_music_path(path_))
    {
        scan.truncated = true;
        return StorageResult::Ok;
    }
    if (entry_.directory)
    {
        if (scan.depth + 1 >= 8)
        {
            scan.truncated = true;
            return StorageResult::Ok;
        }
        ++scan.depth;
        copy_text(scan.directories[scan.depth].path, 256, path_);
        scan.directories[scan.depth].cursor = 0;
        return StorageResult::Ok;
    }
    if (!is_mp3(entry_.name) || entry_.size > UINT32_MAX || entry_.size < 4) return StorageResult::Ok;
    auto* song = storage_.open(path_, FileMode::Read);
    if (!song)
    {
        if (storage_.last_result() == StorageResult::Busy)
        {
            dir.cursor = previous;
            return StorageResult::Busy;
        }
        return StorageResult::Ok;
    }
    scratch_ = {};
    copy_text(scratch_.path, sizeof(scratch_.path), path_);
    scratch_.id = path_id(path_);
    const bool parsed = scan.tags.read(*song, scratch_);
    storage_.close(song);
    if (!parsed)
    {
        if (storage_.last_result() == StorageResult::Busy)
        {
            dir.cursor = previous;
            return StorageResult::Busy;
        }
        return StorageResult::Ok;
    }
    if (scan.count == kMaxTracks)
    {
        scan.truncated = true;
        return StorageResult::Ok;
    }
    const unsigned bucket = scratch_.id & 255;
    if (!write_record(*scan.output, scratch_, scan.heads[bucket]))
    {
        abort_scan();
        return StorageResult::Error;
    }
    scan.heads[bucket] = scan.count + 1;
    for (unsigned kind = 0; kind < 3; kind++)
    {
        const auto id = path_id(group_name(scratch_, kind, scan.folder, sizeof(scan.folder)));
        unsigned n = 0;
        for (; n < scan.group_count[kind] && scan.groups[kind][n].id != id; n++)
        {
        }
        if (n == 512)
        {
            scan.truncated = true;
            continue;
        }
        auto& group = scan.groups[kind][n];
        if (n == scan.group_count[kind])
        {
            group.id = id;
            group.first = scan.count;
            scan.group_count[kind]++;
        }
        group.count++;
    }
    ++scan.count;
    return StorageResult::Ok;
}
bool Catalog::browse(Snapshot& out, uint8_t rows)
{
    rows = std::min<uint8_t>(rows, kPageRows);
    out.row_count = 0;
    if (out.browse == Browse::Artists || out.browse == Browse::Albums || out.browse == Browse::Folders)
    {
        const unsigned kind = static_cast<unsigned>(out.browse) - 1;
        out.total = groups_[kind];
        index_path(lane_, types[kind], path_, sizeof(path_));
        auto* file = storage_.open(path_, FileMode::Read);
        if (!file) return false;
        for (uint32_t index = out.offset; index < out.total && out.row_count < rows; index++)
        {
            if (file->read_at(index * 16, record_, 16) != 16)
            {
                storage_.close(file);
                return false;
            }
            const auto id = get64(record_);
            const auto first = get32(record_ + 8), count = get32(record_ + 12);
            if (!read_record(first, scratch_))
            {
                storage_.close(file);
                return false;
            }
            auto& row = out.rows[out.row_count++];
            row = {};
            row.id = id;
            row.count = count;
            copy_text(row.title, sizeof(row.title), group_name(scratch_, kind, path_, sizeof(path_)));
            std::snprintf(row.subtitle, sizeof(row.subtitle), "%lu songs", static_cast<unsigned long>(count));
        }
        storage_.close(file);
        return true;
    }
    if (out.browse != Browse::Songs && out.browse != Browse::ArtistTracks && out.browse != Browse::AlbumTracks && out.browse != Browse::FolderTracks) return false;
    out.total = out.browse == Browse::Songs ? count_ : group_size(out.browse, out.category);
    if (!rows || out.offset >= out.total) return true;
    index_path(lane_, "songs", path_, sizeof(path_));
    auto* file = storage_.open(path_, FileMode::Read);
    if (!file) return false;
    uint32_t matching = 0;
    bool ok = true;
    for (uint32_t index = out.browse == Browse::Songs ? out.offset : 0; index < count_ && out.row_count < rows; index++)
    {
        if (yield_ && index % 8 == 0 && !yield_(yield_context_))
        {
            ok = false;
            break;
        }
        if (!read_record_from(*file, index, scratch_))
        {
            ok = false;
            break;
        }
        if (!same_category(scratch_, out.browse, out.category, path_, sizeof(path_))) continue;
        if (out.browse != Browse::Songs && matching++ < out.offset) continue;
        auto& row = out.rows[out.row_count++];
        row = {};
        row.id = scratch_.id;
        copy_text(row.title, sizeof(row.title), scratch_.title);
        std::snprintf(row.subtitle, sizeof(row.subtitle), "%.47s | %.43s", scratch_.artist, scratch_.album);
    }
    storage_.close(file);
    if (!ok) out.row_count = 0;
    return ok;
}
uint32_t Catalog::queue_count(Browse kind, const char* category)
{
    if (kind == Browse::Songs) return count_;
    return group_size(kind, category);
}
uint32_t Catalog::group_size(Browse kind, const char* category)
{
    const unsigned group = kind == Browse::ArtistTracks ? 0 : kind == Browse::AlbumTracks ? 1
                                                                                          : 2;
    index_path(lane_, types[group], path_, sizeof(path_));
    auto* file = storage_.open(path_, FileMode::Read);
    if (!file) return 0;
    uint32_t count = 0;
    const auto id = path_id(category);
    for (unsigned i = 0; i < groups_[group]; i++)
    {
        if (file->read_at(i * 16, record_, 16) != 16) break;
        if (get64(record_) == id)
        {
            count = get32(record_ + 12);
            break;
        }
    }
    storage_.close(file);
    return count;
}
bool Catalog::queue_track(Browse kind, const char* category, uint32_t index, Track& out)
{
    if (kind == Browse::Songs) return track(index, out);
    index_path(lane_, "songs", path_, sizeof(path_));
    auto* file = storage_.open(path_, FileMode::Read);
    if (!file) return false;
    uint32_t matching = 0;
    bool found = false;
    for (uint32_t i = 0; i < count_; i++)
    {
        if (yield_ && i % 8 == 0 && !yield_(yield_context_)) break;
        if (read_record_from(*file, i, scratch_) && same_category(scratch_, kind, category, path_, sizeof(path_)))
            if (matching++ == index)
            {
                out = scratch_;
                found = true;
                break;
            }
    }
    storage_.close(file);
    return found;
}
} // namespace music
