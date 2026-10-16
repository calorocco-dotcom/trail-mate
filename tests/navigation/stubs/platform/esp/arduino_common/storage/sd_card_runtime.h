#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace platform::esp::arduino_common::storage
{
inline uint32_t media = 1;
inline bool ready = true, partial_read = false, partial_write = false;
inline std::map<std::string, std::vector<uint8_t>> files;
inline uint32_t sd_media_session() { return media; }
inline bool sd_card_ready() { return ready; }
enum class SdRuntimeMemoryPolicy
{
    PsramOnly
};
class SdRuntimeFile
{
  public:
    explicit SdRuntimeFile(SdRuntimeMemoryPolicy) {}
    bool open(const char* path, const char* mode, uint32_t expected)
    {
        close();
        busy_ = false;
        if (!ready || expected != media) return false;
        if (!std::strcmp(mode, "wx+"))
        {
            if (files.count(path)) return false;
            files[path] = {};
        }
        else if (!files.count(path)) return false;
        data_ = &files.at(path);
        return true;
    }
    bool is_open() const { return data_ != nullptr; }
    void close()
    {
        data_ = nullptr;
        position_ = 0;
    }
    uint64_t size() const { return data_ ? data_->size() : 0; }
    bool seek(uint64_t offset)
    {
        busy_ = false;
        // SdFat exFAT seekSet rejects positions beyond the file data length.
        if (!data_ || offset > data_->size()) return false;
        position_ = static_cast<size_t>(offset);
        return true;
    }
    int read(void* bytes, size_t count)
    {
        busy_ = false;
        if (!data_ || position_ > data_->size()) return -1;
        count = std::min(count, data_->size() - position_);
        if (partial_read)
        {
            count /= 2;
            busy_ = true;
            partial_read = false;
        }
        std::memcpy(bytes, data_->data() + position_, count);
        position_ += count;
        return static_cast<int>(count);
    }
    size_t write(const void* bytes, size_t count)
    {
        busy_ = false;
        if (!data_) return 0;
        if (partial_write)
        {
            count /= 2;
            busy_ = true;
            partial_write = false;
        }
        data_->resize(std::max(data_->size(), position_ + count));
        std::memcpy(data_->data() + position_, bytes, count);
        position_ += count;
        return count;
    }
    bool flush() { return data_ != nullptr; }
    bool read_busy() const { return busy_; }

  private:
    std::vector<uint8_t>* data_ = nullptr;
    size_t position_ = 0;
    bool busy_ = false;
};
} // namespace platform::esp::arduino_common::storage
