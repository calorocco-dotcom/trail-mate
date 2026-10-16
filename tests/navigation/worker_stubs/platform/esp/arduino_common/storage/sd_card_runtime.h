#pragma once
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace platform::esp::arduino_common::storage
{
inline uint32_t media = 1;
inline bool ready = true;
inline std::map<std::string, std::string> paths;
inline std::set<std::string> directories{"/routes"};
inline std::string output_prefix;
inline size_t open_files = 0;
inline uint64_t read_bytes = 0, write_bytes = 0;
inline uint32_t sd_media_session() { return media; }
inline bool sd_card_ready() { return ready; }
inline bool sd_external_block_owner_active() { return false; }
enum class SdRuntimeMemoryPolicy
{
    PsramOnly
};
enum class SdFileReadStatus
{
    Ready,
    Missing,
    Busy,
    Unavailable,
    IoError
};
enum class SdDirReadStatus
{
    Entry,
    End,
    Busy,
    IoError
};
inline bool sd_exists(const char* path) { return paths.count(path) != 0; }
inline bool sd_is_directory(const char* path) { return ready && directories.count(path); }
inline bool sd_mkdir(const char* path, uint32_t expected)
{
    // SdFat mkdir uses O_EXCL for the final component, including directories.
    return ready && expected == media && !paths.count(path) && directories.insert(path).second;
}
inline bool sd_remove(const char* path, uint32_t expected)
{
    if (!ready || expected != media || !paths.count(path)) return false;
    assert(std::remove(paths.at(path).c_str()) == 0);
    paths.erase(path);
    return true;
}
inline bool sd_rename(const char* from, const char* to, uint32_t expected)
{
    if (!ready || expected != media || !paths.count(from) || paths.count(to)) return false;
    paths[to] = paths.at(from);
    paths.erase(from);
    return true;
}
class SdRuntimeFile
{
  public:
    explicit SdRuntimeFile(SdRuntimeMemoryPolicy = SdRuntimeMemoryPolicy::PsramOnly) {}
    ~SdRuntimeFile() { close(); }
    bool open(const char* path, const char* mode, uint32_t expected)
    {
        close();
        if (!ready || expected != media) return false;
        const bool exclusive = !std::strcmp(mode, "wx+");
        if (exclusive)
        {
            if (paths.count(path)) return false;
            auto name = std::string(path);
            std::replace(name.begin(), name.end(), '/', '_');
            paths[path] = output_prefix + name;
        }
        if (!paths.count(path)) return false;
        file_ = std::fopen(paths.at(path).c_str(), exclusive ? "wb+" : "rb");
        if (file_) ++open_files;
        return file_;
    }
    bool is_open() const { return file_ != nullptr; }
    void close()
    {
        if (file_)
        {
            std::fclose(file_);
            --open_files;
            file_ = nullptr;
        }
    }
    uint64_t size() const
    {
        assert(file_);
        const auto position = _ftelli64(file_);
        assert(_fseeki64(file_, 0, SEEK_END) == 0);
        const auto bytes = _ftelli64(file_);
        assert(_fseeki64(file_, position, SEEK_SET) == 0);
        return static_cast<uint64_t>(bytes);
    }
    bool seek(uint64_t offset) { return file_ && offset <= size() && _fseeki64(file_, static_cast<int64_t>(offset), SEEK_SET) == 0; }
    int read(void* output, size_t count)
    {
        if (!file_) return -1;
        const auto bytes = std::fread(output, 1, count, file_);
        read_bytes += bytes;
        return static_cast<int>(bytes);
    }
    size_t write(const void* input, size_t count)
    {
        if (!file_) return 0;
        const auto bytes = std::fwrite(input, 1, count, file_);
        write_bytes += bytes;
        return bytes;
    }
    bool flush() { return file_ && std::fflush(file_) == 0; }
    bool read_busy() const { return false; }

  private:
    mutable FILE* file_ = nullptr;
};
class SdRuntimeDir
{
  public:
    explicit SdRuntimeDir(SdRuntimeMemoryPolicy = SdRuntimeMemoryPolicy::PsramOnly) {}
    bool is_open() const { return opened_; }
    void close()
    {
        opened_ = false;
        entries_.clear();
        next_ = 0;
    }
    SdFileReadStatus open_read_status(const char* path, uint32_t expected)
    {
        close();
        if (!ready || expected != media) return SdFileReadStatus::Unavailable;
        const auto prefix = std::string(path) + '/';
        for (const auto& pair : paths)
        {
            if (pair.first.compare(0, prefix.size(), prefix)) continue;
            auto name = pair.first.substr(prefix.size());
            if (name.find('/') == std::string::npos) entries_.push_back(name);
        }
        if (entries_.empty()) return SdFileReadStatus::Missing;
        opened_ = true;
        return SdFileReadStatus::Ready;
    }
    SdDirReadStatus read_next_status(char* name, size_t capacity, bool* directory)
    {
        if (next_ == entries_.size()) return SdDirReadStatus::End;
        assert(entries_[next_].size() < capacity);
        std::strcpy(name, entries_[next_++].c_str());
        *directory = false;
        return SdDirReadStatus::Entry;
    }

  private:
    std::vector<std::string> entries_;
    size_t next_ = 0;
    bool opened_ = false;
};
} // namespace platform::esp::arduino_common::storage
