#pragma once
#include "memory_storage.h"
namespace runtime_test
{
inline std::map<std::string, MemoryStorage::Bytes> files;
inline uint32_t session = 1;
inline unsigned storages = 0, ready_calls = 0;
inline void (*on_ready)() = nullptr;
} // namespace runtime_test
namespace platform::esp::music_runtime
{
namespace sd
{
inline uint32_t sd_media_session() { return runtime_test::session; }
inline bool sd_card_ready() { return true; }
inline bool sd_external_block_owner_active() { return false; }
} // namespace sd
class SdStorage final : public MemoryStorage
{
  public:
    SdStorage()
    {
        files = runtime_test::files;
        ++runtime_test::storages;
    }
    ~SdStorage()
    {
        runtime_test::files = std::move(files);
        --runtime_test::storages;
    }
    bool ready() const override
    {
        ++runtime_test::ready_calls;
        if (runtime_test::on_ready) runtime_test::on_ready();
        return true;
    }
    uint32_t session() const override { return sd::sd_media_session(); }
};
bool audio_supported();
bool audio_begin(uint32_t rate);
bool audio_write(const int16_t*, std::size_t);
void audio_end();
} // namespace platform::esp::music_runtime
