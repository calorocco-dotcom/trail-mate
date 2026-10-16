#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
namespace agenda_test
{
inline std::vector<uint8_t> bytes;
inline bool present = false, ready = true, external = false, fail_open = false;
inline uint32_t session = 1;
inline unsigned opens = 0, reads = 0, handles = 0;
inline unsigned change_session_at_read = 0;
} // namespace agenda_test
namespace platform::esp::arduino_common::storage
{
enum class SdRuntimeMemoryPolicy
{
    PsramOnly
};
enum class SdFileReadStatus
{
    Ready,
    Missing,
    IoError
};
struct SdFileReadResult
{
    SdFileReadStatus status;
};
inline uint32_t sd_media_session() { return agenda_test::session; }
inline bool sd_card_ready() { return agenda_test::ready; }
inline bool sd_external_block_owner_active() { return agenda_test::external; }
inline SdFileReadResult sd_read_file(const char*, void*, std::size_t) { return {agenda_test::present ? SdFileReadStatus::IoError : SdFileReadStatus::Missing}; }
// Deliberately exposes no write, mount, format or initialization operation.
class SdRuntimeFile
{
    bool open_ = false;
    std::size_t offset_ = 0;

  public:
    explicit SdRuntimeFile(SdRuntimeMemoryPolicy policy) { assert(policy == SdRuntimeMemoryPolicy::PsramOnly); }
    ~SdRuntimeFile() { close(); }
    bool open(const char*, const char* mode, uint32_t session)
    {
        using namespace agenda_test;
        assert(!std::strcmp(mode, "rb"));
        ++opens;
        if (!present || !ready || external || fail_open || session != sd_media_session()) return false;
        assert(!open_);
        open_ = true;
        offset_ = 0;
        ++handles;
        return true;
    }
    bool is_open() const { return open_; }
    bool read_busy() const { return false; }
    void close()
    {
        if (open_)
        {
            open_ = false;
            --agenda_test::handles;
        }
    }
    uint64_t size() const { return agenda_test::bytes.size(); }
    bool seek(uint64_t offset)
    {
        if (!open_ || offset > size()) return false;
        offset_ = offset;
        return true;
    }
    int read(void* out, std::size_t requested)
    {
        using namespace agenda_test;
        ++reads;
        if (change_session_at_read == reads) ++session;
        if (!open_) return 0;
        const auto count = std::min(requested, bytes.size() - offset_);
        std::memcpy(out, bytes.data() + offset_, count);
        offset_ += count;
        return count;
    }
};
} // namespace platform::esp::arduino_common::storage
