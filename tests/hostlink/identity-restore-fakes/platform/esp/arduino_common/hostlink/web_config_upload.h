#pragma once
#include <cstdint>
#include <cstring>
namespace restore_fake
{
inline bool loaded = false, journal_exists = false;
inline uint8_t phase = 0, kind = 1;
} // namespace restore_fake
namespace platform::esp::arduino_common::storage
{
inline bool sd_exists(const char*) { return restore_fake::journal_exists; }
} // namespace platform::esp::arduino_common::storage
namespace hostlink::management::web_config
{
constexpr const char *kMetaPath = "meta", *kMetaNew = "new", *kMetaBackup = "backup";
struct Metadata
{
    uint8_t owner[16]{}, id[16]{}, hash[32]{}, base_hash[32]{};
    uint32_t total = 0, offset = 0;
    uint8_t phase = 0, kind = 1;
};
inline bool load(Metadata& meta, uint8_t* work)
{
    std::memset(work, 89, 111);
    meta.phase = restore_fake::phase;
    meta.kind = restore_fake::kind;
    return restore_fake::loaded;
}
} // namespace hostlink::management::web_config
