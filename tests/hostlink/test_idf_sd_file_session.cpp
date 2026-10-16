#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/arduino_common/storage/sd_file_lifetime.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace platform::esp::arduino_common::storage
{
static uint32_t media = 1;
static bool s_external_block_owner_active = false, replace_on_lock = false, owner_on_lock = false, lock_available = true;
static void* s_storage_mutex = reinterpret_cast<void*>(1);
static unsigned io_calls = 0;
static bool strict_allocation_fail = false;
static unsigned strict_allocation_attempts = 0;
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++strict_allocation_attempts;
    return strict_allocation_fail ? nullptr : ::operator new(bytes, std::nothrow);
}
struct Info
{
    SdCardBackend backend = SdCardBackend::SdFat;
} s_info;
struct FsFile
{
    bool opened = false;
    void* volume = nullptr; // Real SdFat file state contains a volume pointer.
    explicit operator bool() const { return opened; }
    void close()
    {
        ++io_calls;
        opened = false;
    }
    int available() const
    {
        ++io_calls;
        return 16;
    }
    int read(void*, size_t bytes)
    {
        ++io_calls;
        return static_cast<int>(bytes);
    }
    int read()
    {
        ++io_calls;
        return 7;
    }
    size_t write(const void*, size_t bytes)
    {
        ++io_calls;
        return bytes;
    }
    bool seekSet(uint64_t)
    {
        ++io_calls;
        return true;
    }
    uint64_t curPosition() const
    {
        ++io_calls;
        return 8;
    }
    uint64_t fileSize() const
    {
        ++io_calls;
        return 16;
    }
    bool sync()
    {
        ++io_calls;
        return true;
    }
};
struct Volume
{
    FsFile open(const char*, int) { return {true}; }
} s_volume;
uint32_t sd_media_session() { return media; }
bool path_empty(const char* path) { return !path || !*path; }
const char* normalize_sd_path(const char* path) { return path; }
void copy_path(char* out, size_t bytes, const char* text) { std::snprintf(out, bytes, "%s", text); }
bool open_mode_mutates(const char* mode) { return mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a') || std::strchr(mode, '+')); }
int sdfat_open_flags(const char*) { return 0; }
uint32_t sd_io_begin(const char*, const char*, size_t = 0) { return 0; }
void sd_io_end(const char*, const char*, uint32_t, bool, size_t = 0, int64_t = 0) {}
bool sd_mutation_blocked_by_external_owner(const char*, const char*, uint32_t, size_t = 0) { return s_external_block_owner_active; }
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
        if (owner_on_lock)
        {
            s_external_block_owner_active = true;
            owner_on_lock = false;
        }
    }
    bool locked() const { return lock_available; }
};
#include "idf_sd_file_session_actual.inc"
} // namespace platform::esp::arduino_common::storage
int main()
{
    using namespace platform::esp::arduino_common::storage;
    {
        SdRuntimeFile strict(SdRuntimeMemoryPolicy::PsramOnly);
        assert(strict.has_backing_storage());
        assert(strict_allocation_attempts == 1);
    }
    strict_allocation_fail = true;
    {
        SdRuntimeFile strict(SdRuntimeMemoryPolicy::PsramOnly);
        assert(!strict.has_backing_storage());
        assert(!strict.open("snapshot", "r", media));
        SdRuntimeFile ordinary;
        assert(ordinary.has_backing_storage());
    }
    assert(strict_allocation_attempts == 2);
    strict_allocation_fail = false;
    uint8_t buffer[8] = {};
    for (int transition = 0; transition < 2; ++transition)
    {
        for (int operation = 0; operation < 10; ++operation)
        {
            s_external_block_owner_active = false;
            SdRuntimeFile file;
            assert(file.open("snapshot", "r+b", media));
            const auto before = io_calls;
            if (transition == 0) replace_on_lock = true;
            else owner_on_lock = true;
            switch (operation)
            {
            case 0:
                assert(file.read(buffer, sizeof(buffer)) < 0);
                break;
            case 1:
                assert(file.read_bytes(reinterpret_cast<char*>(buffer), sizeof(buffer)) == 0);
                break;
            case 2:
                assert(file.read_byte() == -1);
                break;
            case 3:
                assert(file.write(buffer, sizeof(buffer)) == 0);
                break;
            case 4:
                assert(!file.seek(0));
                break;
            case 5:
                assert(file.available() == 0);
                break;
            case 6:
                assert(file.size() == 0);
                break;
            case 7:
                assert(file.position() == 0);
                break;
            case 8:
                assert(!file.flush());
                break;
            case 9:
                file.close();
                break;
            }
            assert(io_calls == before && !file.is_open());
            file.close();
            assert(io_calls == before);
        }
    }
    s_external_block_owner_active = false;
    SdRuntimeFile file;
    assert(file.open("snapshot", "r+b", media));
    assert(file.read(buffer, sizeof(buffer)) == 8 && file.seek(0) && file.write(buffer, sizeof(buffer)) == 8 && file.flush());
    const auto before = io_calls;
    lock_available = false;
    assert(file.read(buffer, sizeof(buffer)) == -1 && file.read_busy());
    file.close();
    assert(!file.is_open() && io_calls == before);
    lock_available = true;
    assert(file.open("snapshot", "r+b", media));
    file.close();
    assert(io_calls == before + 1);
    return 0;
}
