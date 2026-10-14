#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/arduino_common/storage/sd_file_lifetime.h"
#include "platform/esp/common/storage/mount_io_state.h"
#include "platform/esp/idf_common/hostlink_track_history_scan.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
namespace platform::esp::arduino_common::storage
{
static bool strict_allocation_fail = false;
static unsigned strict_allocation_attempts = 0;
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++strict_allocation_attempts;
    return strict_allocation_fail ? nullptr : ::operator new(bytes, std::nothrow);
}
static uint32_t media = 1;
static bool s_external_block_owner_active = false, lock_available = true, replace_on_lock = false;
static bool missing = false, open_error = false, entry_end = false, entry_error = false, name_error = false;
static bool read_error = false, replace_on_read = false;
static unsigned closes = 0, next_calls = 0;
static const char* entry_name = "track.gpx";
static bool entry_directory = false;
static uint64_t probe_bytes = 16;
static platform::esp::storage::MountIoState s_mounted;
using SdIoErrorScope = platform::esp::storage::MountIoErrorScope;
constexpr int O_RDONLY = 0;
struct Info
{
    SdCardBackend backend = SdCardBackend::SdFat;
} s_info;
struct FsFile
{
    bool opened = false, directory = true;
    void* volume = nullptr;
    uint64_t cursor = 0;
    explicit operator bool() const { return opened; }
    bool isDir() const { return directory; }
    bool close()
    {
        ++closes;
        opened = false;
        return true;
    }
    FsFile openNextFile(int)
    {
        cursor += 32;
        ++next_calls;
        if (entry_error) s_mounted.markFailure();
        return {!(entry_error || entry_end), entry_directory};
    }
    bool getName(char* name, size_t capacity)
    {
        if (name_error || capacity <= std::strlen(entry_name)) return false;
        std::memcpy(name, entry_name, std::strlen(entry_name) + 1);
        return true;
    }
    int getError() const { return 0; }
    uint64_t fileSize() const { return probe_bytes; }
    uint64_t curPosition() const { return cursor; }
    bool seekSet(uint64_t position)
    {
        cursor = position;
        return true;
    }
    int read(void* output, size_t bytes)
    {
        if (read_error)
        {
            s_mounted.markFailure();
            return -1;
        }
        if (replace_on_read)
        {
            ++media;
            replace_on_read = false;
        }
        std::memset(output, 0x55, bytes);
        return static_cast<int>(bytes);
    }
};
struct Volume
{
    FsFile open(const char*, int)
    {
        if (open_error) s_mounted.markFailure();
        return {!missing && !open_error};
    }
} s_volume;
uint32_t sd_media_session() { return media; }
bool sd_external_block_owner_active() { return s_external_block_owner_active; }
bool sd_card_ready() { return s_mounted && !s_external_block_owner_active; }
bool path_empty(const char* path) { return !path || !*path; }
const char* normalize_sd_path(const char* path) { return path; }
void copy_path(char* out, size_t bytes, const char* path) { std::snprintf(out, bytes, "%s", path); }
uint32_t sd_io_begin(const char*, const char*, size_t = 0) { return 0; }
void sd_io_end(const char*, const char*, uint32_t, bool, size_t = 0, int32_t = 0) {}
class SdRuntimeBusGuard
{
  public:
    explicit SdRuntimeBusGuard(const char*)
    {
        if (replace_on_lock)
        {
            ++media;
            replace_on_lock = false;
        }
    }
    bool locked() const { return lock_available; }
};
#include "idf_sd_directory_actual.inc"
#include "idf_sd_probe_actual.inc"
} // namespace platform::esp::arduino_common::storage
static uint8_t buffer[4096];
int main()
{
    using namespace platform::esp::arduino_common::storage;
    strict_allocation_fail = true;
    {
        SdRuntimeDir failed(SdRuntimeMemoryPolicy::PsramOnly);
        assert(!failed.has_backing_storage() && !failed.open("/trailmate/tracks"));
    }
    strict_allocation_fail = false;
    {
        SdRuntimeDir admitted(SdRuntimeMemoryPolicy::PsramOnly);
        assert(admitted.has_backing_storage());
    }
    assert(strict_allocation_attempts == 2);
    {
        using namespace hostlink::management::publication_runtime;
        s_mounted = true;
        SdRuntimeDir directory(SdRuntimeMemoryPolicy::PsramOnly);
        HistoryScan scan(directory, media);
        track_sharing::PublicationSlotKey key;
        entry_name = "1111111111111111111111111111111111111111111111111111111111111111";
        entry_directory = true;
        lock_available = false;
        assert(scan.step(key) == HistoryScanResult::Busy && !directory.is_open());
        lock_available = true;
        assert(scan.step(key) == HistoryScanResult::Pending && !directory.is_open());
        entry_name = "22222222222222222222222222222222.pua";
        entry_directory = false;
        const auto candidate = scan.step(key);
        assert(candidate == HistoryScanResult::Candidate && !directory.is_open() && key.object[0] == 0x11 && key.operation[0] == 0x22);
        entry_end = true;
        assert(scan.step(key) == HistoryScanResult::Pending && !directory.is_open());
        assert(scan.step(key) == HistoryScanResult::End && !directory.is_open());
        entry_end = false;
        ++media;
        assert(scan.step(key) == HistoryScanResult::Unavailable);
        entry_name = "track.gpx";
    }
    media = 1;
    closes = 0;
    next_calls = 0;
    using namespace platform::esp::arduino_common::storage;
    s_mounted = true;
    SdRuntimeDir dir;
    missing = true;
    assert(dir.open_read_status("/trackers", media) == SdFileReadStatus::Missing);
    open_error = true;
    assert(dir.open_read_status("/trackers", media) == SdFileReadStatus::IoError);
    missing = open_error = false;
    lock_available = false;
    assert(dir.open_read_status("/trackers", media) == SdFileReadStatus::Busy);
    lock_available = true;
    replace_on_lock = true;
    assert(dir.open_read_status("/trackers", media) == SdFileReadStatus::Unavailable);
    assert(dir.open_read_status("/trackers", media) == SdFileReadStatus::Ready);
    char name[16] = {};
    bool is_dir = true;
    lock_available = false;
    assert(dir.read_next_status(name, sizeof(name), &is_dir) == SdDirReadStatus::Busy && next_calls == 0);
    lock_available = true;
    assert(dir.read_next_status(name, sizeof(name), &is_dir) == SdDirReadStatus::Entry && !is_dir && !std::strcmp(name, "track.gpx"));
    entry_end = true;
    assert(dir.read_next_status(name, sizeof(name), &is_dir) == SdDirReadStatus::End);
    entry_error = true;
    assert(dir.read_next_status(name, sizeof(name), &is_dir) == SdDirReadStatus::IoError);
    entry_end = entry_error = false;
    name_error = true;
    const auto before_name = closes;
    assert(dir.read_next_status(name, sizeof(name), &is_dir) == SdDirReadStatus::IoError && closes == before_name + 1 && !name[0]);
    name_error = false;
    const auto before_close = closes;
    replace_on_lock = true;
    assert(dir.read_next_status(name, sizeof(name), &is_dir) == SdDirReadStatus::Unavailable);
    dir.close();
    assert(closes == before_close && !dir.is_open());
    assert(dir.open("/trackers"));
    s_external_block_owner_active = true;
    dir.close();
    assert(closes == before_close);
    s_external_block_owner_active = false;
    missing = true;
    assert(sd_read_file("file", buffer, sizeof(buffer)).status == SdFileReadStatus::Missing);
    open_error = true;
    assert(sd_read_file("file", buffer, sizeof(buffer)).status == SdFileReadStatus::IoError);
    missing = open_error = false;
    read_error = true;
    assert(sd_read_file("file", buffer, sizeof(buffer)).status == SdFileReadStatus::IoError);
    read_error = false;
    replace_on_read = true;
    assert(sd_read_file("file", buffer, sizeof(buffer)).status == SdFileReadStatus::Unavailable);
    probe_bytes = 3000;
    const auto result = sd_read_file("file", buffer, sizeof(buffer));
    assert(result.status == SdFileReadStatus::Ready && result.bytes_read == 3000);
    platform::esp::storage::MountIoState state;
    state = true;
    {
        platform::esp::storage::MountIoErrorScope outer(state);
        {
            platform::esp::storage::MountIoErrorScope inner(state);
            state.markFailure();
            assert(inner.error());
        }
        assert(outer.error());
    }
    {
        platform::esp::storage::MountIoErrorScope fresh(state);
        assert(!fresh.error());
    }
    assert(state && state.failed());
    state = false;
    assert(!state && !state.failed());
    return 0;
}
