#include <cassert>
#include <cstddef>
#include <cstdint>
namespace sys::runtime
{
enum class BusAccessPolicy
{
    DurableCommit
};
}
namespace platform::esp::arduino_common::storage
{
static uint32_t media = 1;
static bool s_external_block_owner_active = false, replace_on_lock = false, owner_on_lock = false, lock_available = true;
static unsigned removes = 0, renames = 0;
constexpr unsigned kSdDurableLockWaitMs = 1;
enum class SdCardBackend
{
    SdFat
};
struct Info
{
    SdCardBackend backend = SdCardBackend::SdFat;
} s_info;
struct Volume
{
    bool remove(const char*)
    {
        ++removes;
        return true;
    }
    bool rename(const char*, const char*)
    {
        ++renames;
        return true;
    }
} s_volume, s_sdfat;
uint32_t sd_media_session() { return media; }
const char* normalize_sd_path(const char* path) { return path; }
uint32_t sd_io_begin(const char*, const char*) { return 0; }
void sd_io_end(const char*, const char*, uint32_t, bool, size_t = 0, int = 0) {}
bool sd_mutation_blocked_by_external_owner(const char*, const char*, uint32_t) { return s_external_block_owner_active; }
class SdRuntimeBusGuard
{
  public:
    explicit SdRuntimeBusGuard(const char*, sys::runtime::BusAccessPolicy = sys::runtime::BusAccessPolicy::DurableCommit, unsigned = 0)
    {
        if (replace_on_lock)
        {
            ++media;
            replace_on_lock = false;
        }
        if (owner_on_lock)
        {
            s_external_block_owner_active = true;
            owner_on_lock = false;
        }
    }
    bool locked() const { return lock_available; }
};
using SdRuntimeOperationGuard = SdRuntimeBusGuard;
bool sd_remove(const char*, uint32_t);
bool sd_rename(const char*, const char*, uint32_t);
#if defined(TRAIL_MATE_SD_MUTATION_ARDUINO)
#include "sd_arduino_remove_actual.inc"
#else
#include "sd_idf_mutations_actual.inc"
#endif
} // namespace platform::esp::arduino_common::storage
int main()
{
    using namespace platform::esp::arduino_common::storage;
    assert(sd_remove("pending", media) && removes == 1);
    assert(!sd_remove("pending", media - 1) && removes == 1);
    replace_on_lock = true;
    assert(!sd_remove("pending") && removes == 1);
    owner_on_lock = true;
    assert(!sd_remove("pending", media) && removes == 1);
    s_external_block_owner_active = false;
    lock_available = false;
    assert(!sd_remove("pending", media) && removes == 1);
    lock_available = true;
#if !defined(TRAIL_MATE_SD_MUTATION_ARDUINO)
    assert(sd_rename("pending", "snapshot", media) && renames == 1);
    assert(!sd_rename("pending", "snapshot", media - 1) && renames == 1);
    replace_on_lock = true;
    assert(!sd_rename("pending", "snapshot") && renames == 1);
    owner_on_lock = true;
    assert(!sd_rename("pending", "snapshot", media) && renames == 1);
#endif
    return 0;
}
