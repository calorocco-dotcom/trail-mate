#pragma once
#include "geocaching/domain/record.h"
#include "track_sharing/file_integrity.h"
namespace track_sharing
{
// Read exact bounded signed CMP bytes into caller-admitted PSRAM. Caller must
// subsequently verifyManifest and match the current head; Ready is only I/O.
template <class Io>
class SignedManifestReader
{
  public:
    explicit SignedManifestReader(Io& io) : io_(io) {}
    FileIntegrityResult begin(const char* path, uint8_t* output, size_t capacity)
    {
        if (result_ != FileIntegrityResult::Idle) return FileIntegrityResult::Busy;
        if (!path || !*path || !output || capacity < 4166) return FileIntegrityResult::Invalid;
        const auto a = reinterpret_cast<uintptr_t>(output);
        const auto overlaps = [&](const void* input, size_t size)
        { const auto b = reinterpret_cast<uintptr_t>(input); return a <= b ? b - a < 4166 : a - b < size; };
        if (overlaps(this, sizeof(*this)) || overlaps(&io_, sizeof(io_)) || overlaps(path, std::strlen(path) + 1)) return FileIntegrityResult::Invalid;
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return result_ = FileIntegrityResult::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size), closed = io_.close(opened.handle);
        if (!sized || !closed) return result_ = FileIntegrityResult::StorageFailure;
        if (size < 69 || size > 4166) return result_ = FileIntegrityResult::Invalid;
        output_ = output;
        path_ = path;
        size_ = uint32_t(size);
        return result_ = FileIntegrityResult::Pending;
    }
    FileIntegrityResult step()
    {
        if (result_ != FileIntegrityResult::Pending) return result_;
        const auto opened = io_.open(path_, Io::Mode::Read);
        if (!opened.handle) return stop(FileIntegrityResult::StorageFailure);
        uint64_t size = 0;
        const size_t count = std::min<size_t>(320, size_ - offset_);
        const bool read = io_.size(opened.handle, size) && size == size_ && io_.seek(opened.handle, offset_) && io_.read(opened.handle, output_ + offset_, count) == count;
        const bool closed = io_.close(opened.handle);
        if (!read || !closed) return stop(FileIntegrityResult::StorageFailure);
        offset_ += uint32_t(count);
        return result_ = offset_ == size_ ? FileIntegrityResult::Ready : FileIntegrityResult::Pending;
    }
    FileIntegrityResult cancel() { return result_ == FileIntegrityResult::Pending ? stop(FileIntegrityResult::Cancelled) : result_; }
    geocaching::ByteView bytes() const { return result_ == FileIntegrityResult::Ready ? geocaching::ByteView{output_, size_} : geocaching::ByteView{}; }

  private:
    FileIntegrityResult stop(FileIntegrityResult result)
    {
        if (output_) std::memset(output_, 0, size_);
        return result_ = result;
    }
    Io& io_;
    const char* path_ = nullptr;
    uint8_t* output_ = nullptr;
    uint32_t size_ = 0, offset_ = 0;
    FileIntegrityResult result_ = FileIntegrityResult::Idle;
};
} // namespace track_sharing
