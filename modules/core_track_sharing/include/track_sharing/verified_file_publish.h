#pragma once
#include "track_sharing/file_integrity.h"
#include "track_sharing/sign_manifest.h"
namespace track_sharing
{
// Requires exclusive canonical paths/media and stable expected hash supplied by
// the admitted owner. Ready includes a complete read-back after rename.
template <class Io, class Hash>
class VerifiedFilePublish
{
  public:
    VerifiedFilePublish(Io& io, Hash& hash) : io_(io), check_(io, hash) {}
    FileIntegrityResult begin(const char* temporary, const char* destination, uint32_t bytes, const uint8_t* expected)
    {
        if (check_.active()) return FileIntegrityResult::Busy;
        result_ = FileIntegrityResult::Invalid;
        if (!temporary || !destination || !expected || !*temporary || !*destination || !std::strcmp(temporary, destination) ||
            !bytes || bytes > 8388608 || signingSpansOverlap(expected, 32, reinterpret_cast<const uint8_t*>(this), sizeof(*this))) return FileIntegrityResult::Invalid;
        temporary_ = temporary; destination_ = destination; expected_ = expected; bytes_ = bytes; final_ = false;
        const auto existing = io_.open(destination, Io::Mode::Read);
        if (existing.handle)
        {
            if (!io_.close(existing.handle)) return result_ = FileIntegrityResult::StorageFailure;
            final_ = true;
        }
        else if (existing.status != Io::OpenStatus::Missing) return result_ = FileIntegrityResult::StorageFailure;
        return result_ = check_.begin(final_ ? destination_ : temporary_, bytes_, expected_);
    }
    FileIntegrityResult step(uint8_t* scratch, size_t capacity)
    {
        if (result_ != FileIntegrityResult::Pending) return result_;
        if (!scratch || signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            signingSpansOverlap(scratch, capacity, expected_, 32) ||
            signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(temporary_), std::strlen(temporary_) + 1) ||
            signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(destination_), std::strlen(destination_) + 1))
        { check_.cancel(); return result_ = FileIntegrityResult::Invalid; }
        const auto checked = check_.step(scratch, capacity);
        if (checked != FileIntegrityResult::Ready) return result_ = checked;
        if (final_) return result_ = FileIntegrityResult::Ready;
        if (!io_.publish(temporary_, destination_)) return result_ = FileIntegrityResult::StorageFailure;
        final_ = true;
        return result_ = check_.begin(destination_, bytes_, expected_);
    }
    FileIntegrityResult cancel() { check_.cancel(); return result_ = FileIntegrityResult::Cancelled; }
  private:
    Io& io_;
    FileIntegrityCheck<Io, Hash> check_;
    const char* temporary_ = nullptr;
    const char* destination_ = nullptr;
    const uint8_t* expected_ = nullptr;
    uint32_t bytes_ = 0;
    bool final_ = false;
    FileIntegrityResult result_ = FileIntegrityResult::Idle;
};
} // namespace track_sharing
