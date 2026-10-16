#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <esp_heap_caps.h>
#include <map>
#include <string>
#include <vector>
namespace image_fake
{
inline uint32_t media = 1;
inline bool ready = true, busy = false, flush_fail = false, write_fail = false, rename_fail = false, open_fail = false;
inline std::map<std::string, std::vector<uint8_t>> files;
inline unsigned probes = 0, reads = 0, writes = 0, removes = 0, renames = 0;
inline void reset()
{
    media = 1;
    ready = true;
    busy = false;
    flush_fail = false;
    write_fail = false;
    rename_fail = false;
    open_fail = false;
    files.clear();
    probes = reads = writes = removes = renames = 0;
}
} // namespace image_fake
namespace platform::esp::arduino_common::storage
{
enum class SdRuntimeMemoryPolicy
{
    Default,
    PsramOnly
};
enum class SdFileReadStatus
{
    Ready,
    Missing,
    Busy,
    Unavailable,
    IoError,
    Invalid
};
struct SdFileReadResult
{
    SdFileReadStatus status;
};
inline bool sd_card_ready() { return image_fake::ready; }
inline uint32_t sd_media_session() { return image_fake::media; }
inline bool sd_external_block_owner_active() { return image_fake::busy; }
inline SdFileReadResult sd_read_file(const char* path, uint8_t* output, size_t capacity)
{
    ++image_fake::probes;
    if (!image_fake::ready) return {SdFileReadStatus::Unavailable};
    if (image_fake::busy) return {SdFileReadStatus::Busy};
    auto it = image_fake::files.find(path);
    if (it == image_fake::files.end()) return {SdFileReadStatus::Missing};
    if (it->second.empty() || it->second.size() > capacity) return {SdFileReadStatus::Invalid};
    if (capacity && !it->second.empty()) *output = it->second[0];
    return {SdFileReadStatus::Ready};
}
inline bool sd_remove(const char* path, uint32_t expected)
{
    ++image_fake::removes;
    return image_fake::ready && expected == image_fake::media && image_fake::files.erase(path) == 1;
}
inline bool sd_rename(const char* from, const char* to, uint32_t expected)
{
    ++image_fake::renames;
    if (image_fake::rename_fail || !image_fake::ready || expected != image_fake::media || !image_fake::files.count(from) || image_fake::files.count(to)) return false;
    image_fake::files[to] = std::move(image_fake::files[from]);
    image_fake::files.erase(from);
    return true;
}
class SdRuntimeFile
{
  public:
    explicit SdRuntimeFile(SdRuntimeMemoryPolicy policy = SdRuntimeMemoryPolicy::Default) : policy_(policy) {}
    ~SdRuntimeFile() { close(); }
    bool has_backing_storage() const { return true; }
    bool open(const char* path, const char* mode, uint32_t expected = 0)
    {
        if (!expected) expected = image_fake::media;
        if (opened_ || !image_fake::ready || image_fake::busy || image_fake::open_fail || expected != image_fake::media) return false;
        assert(policy_ == (image_fake::psram ? SdRuntimeMemoryPolicy::PsramOnly : SdRuntimeMemoryPolicy::Default));
        path_ = path;
        media_ = expected;
        offset_ = 0;
        if (*mode == 'r' && !image_fake::files.count(path_)) return false;
        if (*mode == 'w') image_fake::files[path_].clear();
        opened_ = true;
        return true;
    }
    void close() { opened_ = false; }
    bool is_open() const { return opened_; }
    bool read_busy() const { return image_fake::busy; }
    uint64_t size() const { return opened_ && media_ == image_fake::media ? image_fake::files.at(path_).size() : 0; }
    uint64_t position() const { return opened_ ? offset_ : 0; }
    bool seek(uint64_t at)
    {
        if (!opened_ || at > size()) return false;
        offset_ = size_t(at);
        return true;
    }
    int read(void* output, size_t count)
    {
        ++image_fake::reads;
        if (!opened_ || image_fake::busy || media_ != image_fake::media) return -1;
        const auto& bytes = image_fake::files.at(path_);
        count = std::min(count, bytes.size() - offset_);
        std::memcpy(output, bytes.data() + offset_, count);
        offset_ += count;
        return int(count);
    }
    size_t write(const void* input, size_t count)
    {
        ++image_fake::writes;
        if (!opened_ || image_fake::write_fail || media_ != image_fake::media) return 0;
        auto& bytes = image_fake::files.at(path_);
        const auto* data = static_cast<const uint8_t*>(input);
        bytes.insert(bytes.end(), data, data + count);
        return count;
    }
    bool flush() { return opened_ && !image_fake::flush_fail && media_ == image_fake::media; }

  private:
    SdRuntimeMemoryPolicy policy_;
    std::string path_;
    size_t offset_ = 0;
    uint32_t media_ = 0;
    bool opened_ = false;
};
} // namespace platform::esp::arduino_common::storage
