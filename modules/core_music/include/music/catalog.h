#pragma once
#include "music/id3.h"
#include "music/storage.h"
namespace music
{
class Catalog
{
    struct Scan;
    Storage& storage_;
    Scan* scan_ = nullptr;
    uint32_t session_ = 0, generation_ = 0, count_ = 0, groups_[3]{};
    uint32_t heads_[256]{};
    uint8_t lane_ = 0;
    uint8_t record_[580]{};
    uint8_t manifest_[1088]{};
    Track scratch_{};
    Entry entry_{};
    char path_[320]{};
    bool truncated_ = false;
    bool read_record_from(File& file, uint32_t index, Track& out, uint32_t* link = nullptr);
    uint32_t group_size(Browse kind, const char* category);
    void* yield_context_ = nullptr;
    bool (*yield_)(void*) = nullptr;
    bool load_lane(uint8_t lane, bool apply);
    bool read_record(uint32_t index, Track& out, uint32_t* link = nullptr);
    bool write_record(File& output, const Track& track, uint32_t link);
    void index_path(unsigned lane, const char* type, char* out, std::size_t capacity) const;

  public:
    explicit Catalog(Storage& storage) : storage_(storage) {}
    ~Catalog();
    bool load();
    bool begin_scan();
    StorageResult scan_step();
    void abort_scan();
    bool scanning() const { return scan_ != nullptr; }
    uint32_t count() const { return count_; }
    bool track(uint32_t index, Track& out) { return read_record(index, out); }
    bool find(uint64_t id, Track& out, uint32_t* index = nullptr);
    bool browse(Snapshot& out, uint8_t rows);
    bool queue_track(Browse kind, const char* category, uint32_t index, Track& out);
    uint32_t queue_count(Browse kind, const char* category);
    bool truncated() const;
    bool cooperate() { return !yield_ || yield_(yield_context_); }
    void set_yield(bool (*callback)(void*), void* context)
    {
        yield_ = callback;
        yield_context_ = context;
    }
};
} // namespace music
