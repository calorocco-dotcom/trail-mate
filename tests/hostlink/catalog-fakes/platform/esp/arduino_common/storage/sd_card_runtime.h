#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace platform::esp::arduino_common::storage
{
enum class SdFileReadStatus
{
    Ready,
    Missing,
    Busy,
    Unavailable,
    Invalid,
    IoError
};
enum class SdDirReadStatus
{
    Entry,
    End,
    Busy,
    Unavailable,
    Invalid,
    IoError
};
struct Row
{
    std::string name;
    bool directory = false;
    uint64_t bytes = 100;
};
inline std::vector<Row> rows;
inline uint32_t media = 1;
inline bool busy = false, unavailable = false, external = false;
inline unsigned scans = 0;
inline uint32_t sd_media_session() { return media; }
inline bool sd_external_block_owner_active() { return external; }
class SdRuntimeDir
{
    size_t index = 0;

  public:
    SdFileReadStatus open_read_status(const char*, uint32_t expected)
    {
        if (busy) return SdFileReadStatus::Busy;
        if (unavailable || expected != media) return SdFileReadStatus::Unavailable;
        return SdFileReadStatus::Ready;
    }
    bool seek_read_position(uint64_t position)
    {
        index = position / 32;
        return index <= rows.size();
    }
    SdFileReadStatus seek_read_status(uint64_t position) { return seek_read_position(position) ? SdFileReadStatus::Ready : SdFileReadStatus::Invalid; }
    SdDirReadStatus read_next_info(char* name, size_t capacity, bool* directory, uint64_t* bytes, uint64_t* next)
    {
        ++scans;
        if (index == rows.size()) return SdDirReadStatus::End;
        const auto& row = rows[index++];
        if (row.name.size() >= capacity) return SdDirReadStatus::IoError;
        std::memcpy(name, row.name.c_str(), row.name.size() + 1);
        *directory = row.directory;
        *bytes = row.bytes;
        *next = index * 32;
        return SdDirReadStatus::Entry;
    }
};
} // namespace platform::esp::arduino_common::storage
