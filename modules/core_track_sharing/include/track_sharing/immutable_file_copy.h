#pragma once
#include "track_sharing/file_copy_slice.h"
#include "track_sharing/verified_file_publish.h"
#include "track_sharing/file_prefix_check.h"
namespace track_sharing
{
enum class ImmutableCopyResult : uint8_t { Idle, Pending, Ready, Conflict, HashMismatch, StorageFailure, Invalid, Cancelled };
// The admitted owner provides exclusive device-derived paths, media-bound Io,
// hash context and per-tick scratch. Partial orphan files are preserved.
template <class Io, class Hash>
class ImmutableFileCopy
{
  public:
    ImmutableFileCopy(Io& io, Hash& hash) : io_(io), publisher_(io, hash) {}
    ImmutableFileCopy(const ImmutableFileCopy&) = delete;
    ImmutableFileCopy& operator=(const ImmutableFileCopy&) = delete;
    ImmutableCopyResult begin(const char* source, const char* temporary, const char* destination, uint32_t bytes, const uint8_t* expected)
    {
        if (result_ == ImmutableCopyResult::Pending) return ImmutableCopyResult::Conflict;
        result_ = ImmutableCopyResult::Invalid;
        if (!source || !temporary || !destination || !expected || !*source || !*temporary || !*destination || !bytes || bytes > 8388608 ||
            !std::strcmp(source, temporary) || !std::strcmp(source, destination) || !std::strcmp(temporary, destination)) return result_;
        const auto* self = reinterpret_cast<const uint8_t*>(this);
        if (signingSpansOverlap(expected, 32, self, sizeof(*this)) ||
            signingSpansOverlap(reinterpret_cast<const uint8_t*>(source), std::strlen(source) + 1, self, sizeof(*this)) ||
            signingSpansOverlap(reinterpret_cast<const uint8_t*>(temporary), std::strlen(temporary) + 1, self, sizeof(*this)) ||
            signingSpansOverlap(reinterpret_cast<const uint8_t*>(destination), std::strlen(destination) + 1, self, sizeof(*this))) return result_;
        source_ = source; temporary_ = temporary; destination_ = destination; bytes_ = bytes; offset_ = 0; prefix_checked_ = 0;
        std::memcpy(expected_, expected, 32);
        const auto final = io_.open(destination_, Io::Mode::Read);
        if (final.handle)
        {
            if (!io_.close(final.handle)) return result_ = ImmutableCopyResult::StorageFailure;
            copying_ = false; return result_ = convert(publisher_.begin(temporary_, destination_, bytes_, expected_));
        }
        if (final.status != Io::OpenStatus::Missing) return result_ = ImmutableCopyResult::StorageFailure;
        const auto temporary_file = io_.open(temporary_, Io::Mode::Read);
        if (temporary_file.handle)
        {
            uint64_t length = 0;const bool sized = io_.size(temporary_file.handle, length), closed = io_.close(temporary_file.handle);
            if (!sized || !closed) return result_ = ImmutableCopyResult::StorageFailure;
            if (length == bytes_) { copying_ = false; return result_ = convert(publisher_.begin(temporary_, destination_, bytes_, expected_)); }
            if (length > bytes_) return result_ = ImmutableCopyResult::Conflict;
            offset_ = uint32_t(length);
        }
        else
        {
            if (temporary_file.status != Io::OpenStatus::Missing) return result_ = ImmutableCopyResult::StorageFailure;
            const auto created = io_.open(temporary_, Io::Mode::CreateTemporary);
            if (!created.handle) return result_ = ImmutableCopyResult::StorageFailure;
            const bool synced = io_.sync(created.handle), closed = io_.close(created.handle);
            if (!synced || !closed) return result_ = ImmutableCopyResult::StorageFailure;
        }
        copying_ = true; resuming_ = offset_ != 0; return result_ = ImmutableCopyResult::Pending;
    }
    ImmutableCopyResult step(uint8_t* scratch, size_t capacity)
    {
        if (result_ != ImmutableCopyResult::Pending) return result_;
        if (!scratch || signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this)))
        { publisher_.cancel(); return result_ = ImmutableCopyResult::Invalid; }
        if (!copying_) return result_ = convert(publisher_.step(scratch, capacity));
        if (prefix_checked_ < offset_ && resuming_)
        {
            const auto checked = checkFilePrefixSlice(io_, source_, temporary_, bytes_, offset_, prefix_checked_, scratch, capacity, prefix_checked_);
            if (checked == FileCopyResult::Copied) { resuming_ = false; return result_; }
            if (checked == FileCopyResult::Pending) return result_;
            return result_ = checked == FileCopyResult::Conflict ? ImmutableCopyResult::Conflict : checked == FileCopyResult::Invalid ? ImmutableCopyResult::Invalid : ImmutableCopyResult::StorageFailure;
        }
        const auto copied = copyFileSlice(io_, source_, temporary_, bytes_, offset_, scratch, capacity, offset_);
        if (copied == FileCopyResult::Pending) return result_;
        if (copied != FileCopyResult::Copied) return result_ = copied == FileCopyResult::Conflict ? ImmutableCopyResult::Conflict : copied == FileCopyResult::Invalid ? ImmutableCopyResult::Invalid : ImmutableCopyResult::StorageFailure;
        copying_ = false;return result_ = convert(publisher_.begin(temporary_, destination_, bytes_, expected_));
    }
    ImmutableCopyResult cancel() { publisher_.cancel(); return result_ = ImmutableCopyResult::Cancelled; }
  private:
    static ImmutableCopyResult convert(FileIntegrityResult result)
    {
        return result == FileIntegrityResult::Pending ? ImmutableCopyResult::Pending : result == FileIntegrityResult::Ready ? ImmutableCopyResult::Ready :
               result == FileIntegrityResult::HashMismatch ? ImmutableCopyResult::HashMismatch : result == FileIntegrityResult::Invalid ? ImmutableCopyResult::Invalid : ImmutableCopyResult::StorageFailure;
    }
    Io& io_;
    VerifiedFilePublish<Io, Hash> publisher_;
    const char* source_ = nullptr;const char* temporary_ = nullptr;const char* destination_ = nullptr;
    uint8_t expected_[32]{};
    uint32_t bytes_ = 0, offset_ = 0, prefix_checked_ = 0;
    bool copying_ = false, resuming_ = false;
    ImmutableCopyResult result_ = ImmutableCopyResult::Idle;
};
} // namespace track_sharing
