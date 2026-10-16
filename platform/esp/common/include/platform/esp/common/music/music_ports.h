#pragma once
#include "music/storage.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
namespace platform::esp::music_runtime
{
namespace sd = ::platform::esp::arduino_common::storage;
class SdStorage final : public ::music::Storage
{
    class Handle final : public ::music::File
    {
      public:
        SdStorage* owner = nullptr;
        sd::SdRuntimeFile file{sd::SdRuntimeMemoryPolicy::PsramOnly};
        bool used = false;
        uint32_t size() const override;
        std::size_t read_at(uint32_t offset, void* out, std::size_t size) override;
        std::size_t write(const void* input, std::size_t size) override;
        bool flush() override;
    } handles_[8];
    sd::SdRuntimeDir directory_{sd::SdRuntimeMemoryPolicy::PsramOnly};
    char directory_path_[256]{};
    uint32_t directory_session_ = 0;
    mutable ::music::StorageResult result_ = ::music::StorageResult::Ok;

  public:
    SdStorage();
    ~SdStorage();
    bool ready() const override;
    uint32_t session() const override;
    ::music::File* open(const char* path, ::music::FileMode mode) override;
    void close(::music::File*& file) override;
    ::music::StorageResult last_result() const override { return result_; }
    ::music::StorageResult next(const char* directory, uint64_t& cursor, ::music::Entry& entry) override;
    bool exists(const char* path) override;
    bool mkdir(const char* path) override;
    bool remove(const char* path) override;
    bool rename(const char* from, const char* to) override;
};
bool audio_supported();
bool audio_begin(uint32_t rate);
bool audio_write(const int16_t* samples, std::size_t count);
void audio_end();
} // namespace platform::esp::music_runtime
