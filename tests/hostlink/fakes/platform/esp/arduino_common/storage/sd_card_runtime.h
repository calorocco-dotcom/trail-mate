#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
namespace platform::esp::arduino_common::storage
{
inline std::string fake_file = "TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\nEND\n";
inline std::unordered_map<std::string, std::string> fake_files;
inline bool fake_available = true, fake_read_failure = false, fake_write_failure = false;
inline bool sd_exists(const char* path) { return fake_available && (std::string(path) == "/trailmate/config.tms" || fake_files.count(path)); }
inline bool sd_remove(const char* path) { return fake_available && fake_files.erase(path) != 0; }
class SdRuntimeFile
{
    size_t offset_ = 0;
    std::string path_;
    std::string& contents() const { return path_ == "/trailmate/config.tms" ? fake_file : fake_files[path_]; }

  public:
    bool open(const char* path, const char* mode)
    {
        if (!fake_available) return false;
        path_ = path;
        offset_ = 0;
        if (mode[0] == 'w') contents().clear();
        else if (!sd_exists(path)) return false;
        return true;
    }
    void close() {}
    uint64_t size() const { return contents().size(); }
    bool seek(uint64_t offset)
    {
        offset_ = static_cast<size_t>(offset);
        return offset <= contents().size();
    }
    int read(void* output, size_t length)
    {
        if (fake_read_failure || offset_ > contents().size() || length > contents().size() - offset_) return -1;
        std::memcpy(output, contents().data() + offset_, length);
        offset_ += length;
        return static_cast<int>(length);
    }
    int read_byte()
    {
        uint8_t byte;
        return read(&byte, 1) == 1 ? byte : -1;
    }
    size_t write(const void* bytes, size_t length)
    {
        if (fake_write_failure) return 0;
        if (offset_ + length > contents().size()) contents().resize(offset_ + length);
        std::memcpy(contents().data() + offset_, bytes, length);
        offset_ += length;
        return length;
    }
    bool flush() { return !fake_write_failure; }
};
} // namespace platform::esp::arduino_common::storage
