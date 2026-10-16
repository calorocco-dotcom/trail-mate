#pragma once
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/navigation_trace.h"
#include "tmap/route_archive.h"
#include <algorithm>
#include <cinttypes>

namespace platform::esp::navigation
{
// One owner/task per adapter. Partial transfers retry from their original
// offset: writes are idempotent even when physical file size already advanced.
class SdRouteFile final : public tmap::WritableRouteFile
{
  public:
    bool openNew(const char* path, uint32_t session)
    {
        close();
        session_ = session;
        return file_.open(path, "wx+", session);
    }
    tmap::Status openRead(const char* path, uint32_t session)
    {
        if (!file_.is_open())
        {
            session_ = session;
            if (!file_.open(path, "r", session)) return file_.read_busy() ? tmap::Status::Busy : tmap::Status::IoError;
        }
        if (!valid()) return tmap::Status::IoError;
        const auto length = file_.size();
        if (file_.read_busy()) return tmap::Status::Busy;
        length_ = length;
        return length ? tmap::Status::Ok : tmap::Status::Invalid;
    }
    bool busy() const { return file_.read_busy(); }
    bool flush() { return valid() && file_.flush(); }
    void close()
    {
        file_.close();
        length_ = 0;
    }
    uint64_t size() const override { return length_; }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (!valid() || offset > length_ || count > length_ - offset) return tmap::Status::Invalid;
        if (!file_.seek(offset)) return ioFailure("read_seek", offset, count, 0);
        const auto read = file_.read(output, count);
        return read >= 0 && static_cast<size_t>(read) == count ? tmap::Status::Ok : ioFailure("read", offset, count, read > 0 ? static_cast<size_t>(read) : 0);
    }
    tmap::Status writeAt(uint64_t offset, const uint8_t* bytes, size_t count) override
    {
        if (!valid() || offset > UINT64_MAX - count) return tmap::Status::Invalid;
        if (offset > length_)
        {
            // exFAT seekSet cannot jump beyond EOF. Keep extension bounded and
            // idempotent across More/Busy; the constant padding lives in Flash.
            static const uint8_t zero[256]{};
            if (!file_.seek(length_)) return ioFailure("grow_seek", length_, count, 0);
            const auto gap = static_cast<size_t>(std::min<uint64_t>(offset - length_, sizeof(zero)));
            const auto written = file_.write(zero, gap);
            if (written > gap) return ioFailure("grow_write", length_, gap, written);
            length_ += written;
            if (written != gap) return ioFailure("grow_write", length_ - written, gap, written);
            return tmap::Status::More;
        }
        if (!file_.seek(offset)) return ioFailure("write_seek", offset, count, 0);
        const auto written = file_.write(bytes, count);
        if (written > count) return ioFailure("write", offset, count, written);
        length_ = std::max(length_, offset + written);
        return written == count ? tmap::Status::Ok : ioFailure("write", offset, count, written);
    }

  private:
    bool valid() const
    {
        return file_.is_open() && session_ == arduino_common::storage::sd_media_session() &&
               arduino_common::storage::sd_card_ready();
    }
    tmap::Status ioFailure(const char* operation, uint64_t offset, size_t requested, size_t transferred) const
    {
        const auto result = incomplete();
        if (result == tmap::Status::Busy) return result;
        ::platform::ui::navigation::trace("[Navigation][storage_io] adapter=route operation=%s offset=%" PRIu64 " requested=%u transferred=%u physical_bytes=%" PRIu64 " session_match=%u sd_ready=%u\n",
                                          operation, offset, static_cast<unsigned>(requested), static_cast<unsigned>(transferred), length_,
                                          unsigned(session_ == arduino_common::storage::sd_media_session()), unsigned(arduino_common::storage::sd_card_ready()));
        return result;
    }
    tmap::Status incomplete() const { return file_.read_busy() ? tmap::Status::Busy : tmap::Status::IoError; }
    arduino_common::storage::SdRuntimeFile file_{arduino_common::storage::SdRuntimeMemoryPolicy::PsramOnly};
    uint64_t length_ = 0;
    uint32_t session_ = 0;
};
} // namespace platform::esp::navigation
