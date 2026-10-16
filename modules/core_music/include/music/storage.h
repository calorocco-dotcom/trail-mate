#pragma once
#include "music/reader.h"
namespace music
{
enum class FileMode : uint8_t
{
    Read,
    Write,
    Append
};
enum class StorageResult : uint8_t
{
    Ok,
    End,
    Busy,
    Missing,
    Unavailable,
    Error
};
struct Entry
{
    char name[256]{};
    bool directory = false;
    uint64_t size = 0;
};
class File : public Reader
{
  public:
    virtual std::size_t write(const void* data, std::size_t count) = 0;
    virtual bool flush() = 0;
};
class Storage
{
  public:
    virtual ~Storage() = default;
    virtual bool ready() const = 0;
    virtual uint32_t session() const = 0;
    virtual File* open(const char* path, FileMode mode) = 0;
    virtual void close(File*& file) = 0;
    virtual StorageResult last_result() const = 0;
    virtual StorageResult next(const char* directory, uint64_t& cursor, Entry& entry) = 0;
    virtual bool exists(const char* path) = 0;
    virtual bool mkdir(const char* path) = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool rename(const char* from, const char* to) = 0;
};
uint64_t path_id(const char* text);
bool valid_music_path(const char* path);
void copy_text(char* output, std::size_t capacity, const char* text);
} // namespace music
