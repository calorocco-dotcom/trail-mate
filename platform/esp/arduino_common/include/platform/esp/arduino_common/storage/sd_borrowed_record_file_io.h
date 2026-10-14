#pragma once

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/storage/record_file_io.h"

namespace platform::esp::arduino_common::storage
{
// Operation-scoped adapter for an exclusive caller-owned file. The caller
// includes SdRuntimeFile::Impl in resource admission; this adapter allocates
// no wrapper/Impl and never binds itself to a replacement card. Only one
// handle can be open; it is closed between copy slices. No shared backend is
// changed and no internal-flash fallback is permitted.
class SdBorrowedRecordFileIo final : public platform::esp::storage::RecordFileIo
{
  public:
    SdBorrowedRecordFileIo(SdRuntimeFile& file, uint32_t session) : file_(file), session_(session) {}
    ~SdBorrowedRecordFileIo() override
    {
        if (active_) close(&file_);
    }
    SdBorrowedRecordFileIo(const SdBorrowedRecordFileIo&) = delete;
    SdBorrowedRecordFileIo& operator=(const SdBorrowedRecordFileIo&) = delete;
    OpenResult open(const char* path, Mode mode) override
    {
        if (!path || !*path) return {};
        if (!available()) return {nullptr, OpenStatus::Unavailable};
        if (active_ || file_.is_open()) return {};
        const char* flags = mode == Mode::Read ? "rb" : mode == Mode::Update ? "r+b"
                                                                             : "wb";
        if (file_.open(path, flags, session_))
        {
            active_ = true;
            return {&file_, OpenStatus::Ready};
        }
        if (!available()) return {nullptr, OpenStatus::Unavailable};
        uint8_t probe = 0;
        const bool missing = mode == Mode::Read && sd_read_file(path, &probe, 1).status == SdFileReadStatus::Missing;
        if (!available()) return {nullptr, OpenStatus::Unavailable};
        return {nullptr, missing ? OpenStatus::Missing : OpenStatus::IoError};
    }
    bool close(Handle handle) override
    {
        if (!active_ || handle != &file_) return false;
        const bool ready = usable(handle);
        file_.close();
        active_ = false;
        // As with SdRecordFileIo, sync establishes durability, not close.
        return ready;
    }
    bool size(Handle handle, uint64_t& bytes) override
    {
        bytes = 0;
        if (!usable(handle)) return false;
        bytes = file_.size();
        return true;
    }
    bool seek(Handle handle, uint64_t offset) override { return usable(handle) && file_.seek(offset); }
    size_t read(Handle handle, void* buffer, size_t bytes) override
    {
        if (!usable(handle)) return 0;
        const int count = file_.read(buffer, bytes);
        return count > 0 ? static_cast<size_t>(count) : 0;
    }
    size_t write(Handle handle, const void* buffer, size_t bytes) override { return usable(handle) ? file_.write(buffer, bytes) : 0; }
    bool sync(Handle handle) override { return usable(handle) && file_.flush(); }
    bool publish(const char* temporary, const char* destination) override
    {
        if (!temporary || !*temporary || !destination || !*destination || active_ || !available()) return false;
        uint8_t probe = 0;
        return sd_read_file(destination, &probe, 1).status == SdFileReadStatus::Missing && available() && sd_rename(temporary, destination, session_);
    }

  private:
    bool available() const { return session_ == sd_media_session() && sd_card_ready() && !sd_external_block_owner_active(); }
    bool usable(Handle handle) const { return active_ && handle == &file_ && available() && file_.is_open(); }
    SdRuntimeFile& file_;
    uint32_t session_;
    bool active_ = false;
};
static_assert(sizeof(SdBorrowedRecordFileIo) <= 24, "Borrowed SD adapter must not own a driver/buffer");
} // namespace platform::esp::arduino_common::storage
