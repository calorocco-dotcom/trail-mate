#pragma once
#include "geocaching/domain/record.h"
#include "track_sharing/file_integrity.h"
#include "track_sharing/upload_journal.h"
namespace track_sharing
{
// Caller owns an immutable structured-upload path/journal lease and admitted PSRAM
// output. Copy exactly the bytes hashed, never reread after hash verification.
template <class Io, class Hash, size_t MaximumBytes = 4096>
class UploadedManifestReader
{
    static_assert(MaximumBytes > 0 && MaximumBytes <= 4166, "Structured upload reader must remain bounded");

  public:
    UploadedManifestReader(Io& io, Hash& hash, const UploadJournal& journal, const uint32_t& generation, const uint8_t* author)
        : check_(io, hash), journal_(journal), generation_(generation), author_(author) {}
    FileIntegrityResult begin(const char* path, uint8_t* output, size_t capacity)
    {
        if (output_) return FileIntegrityResult::Busy;
        if (!output || !uploadJournalValid(journal_) || journal_.phase != UploadPhase::Sealed || journal_.total > MaximumBytes || capacity < journal_.total || !identityMatches()) return FileIntegrityResult::Invalid;
        const auto target = reinterpret_cast<uintptr_t>(output);
        const auto overlaps = [&](const void* source, size_t length)
        { const auto a = reinterpret_cast<uintptr_t>(source); return a <= target ? target - a < length : a - target < journal_.total; };
        if (!path || overlaps(this, sizeof(*this)) || overlaps(&journal_, sizeof(journal_)) || overlaps(author_, 32) || overlaps(&generation_, sizeof(generation_)) || overlaps(path, std::strlen(path) + 1)) return FileIntegrityResult::Invalid;
        const auto result = check_.begin(path, journal_.total, journal_.file_hash);
        if (result == FileIntegrityResult::Pending)
        {
            output_ = output;
            bytes_ = journal_.total;
        }
        return result_ = result;
    }
    FileIntegrityResult step(uint8_t* scratch, size_t capacity)
    {
        if (result_ != FileIntegrityResult::Pending) return result_;
        if (!identityMatches()) return stop(FileIntegrityResult::Invalid);
        const auto a = reinterpret_cast<uintptr_t>(scratch);
        const auto overlaps = [&](const void* source, size_t length)
        { const auto b = reinterpret_cast<uintptr_t>(source); return a <= b ? b - a < capacity : a - b < length; };
        if (!scratch || overlaps(output_, bytes_) || overlaps(this, sizeof(*this)) || overlaps(&journal_, sizeof(journal_)) ||
            overlaps(author_, 32) || overlaps(&generation_, sizeof(generation_))) return stop(FileIntegrityResult::Invalid);
        const auto before = check_.checkedBytes();
        const auto status = check_.step(scratch, capacity);
        const auto count = check_.checkedBytes() - before;
        if (count) std::memcpy(output_ + before, scratch, count);
        if (status == FileIntegrityResult::Pending) return result_;
        if (status != FileIntegrityResult::Ready) return stop(status);
        return result_ = status;
    }
    FileIntegrityResult cancel() { return result_ == FileIntegrityResult::Pending ? stop(FileIntegrityResult::Cancelled) : result_; }
    geocaching::ByteView bytes() const { return result_ == FileIntegrityResult::Ready && identityMatches() ? geocaching::ByteView{output_, bytes_} : geocaching::ByteView{}; }

  private:
    bool identityMatches() const { return author_ && generation_ && generation_ == journal_.generation && !std::memcmp(author_, journal_.author_hash, 32); }
    FileIntegrityResult stop(FileIntegrityResult status)
    {
        check_.cancel();
        if (output_) std::memset(output_, 0, bytes_);
        return result_ = status;
    }
    FileIntegrityCheck<Io, Hash> check_;
    const UploadJournal& journal_;
    const uint32_t& generation_;
    const uint8_t* author_;
    uint8_t* output_ = nullptr;
    uint32_t bytes_ = 0;
    FileIntegrityResult result_ = FileIntegrityResult::Idle;
};
} // namespace track_sharing
