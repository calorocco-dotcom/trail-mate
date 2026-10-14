#pragma once
#include "track_sharing/attachment_index_stream.h"
#include "track_sharing/file_integrity.h"

namespace track_sharing
{
enum class AttachmentIndexFileResult : uint8_t
{
    Idle,
    Pending,
    Entry,
    Ready,
    Busy,
    Invalid,
    HashMismatch,
    StorageFailure,
    Cancelled
};
// Single-use, caller-admitted workspace (PSRAM on supported ESP boards). I/O
// handles close before yielding. Caller keeps the immutable source/media lease
// and a separate resource-hash context while this index hash remains active.
// Entry is provisional: no publication/install proof until index Ready AND
// every required resource's independently verified bytes match its declaration.
template <class Io, class Hash>
class AttachmentIndexFileCheck
{
  public:
    AttachmentIndexFileCheck(Io& io, Hash& hash, uint8_t* window, size_t capacity,
                             AttachmentFingerprint* fingerprints, size_t slots,
                             const AttachmentIndexBinding& binding, geocaching::ByteView kml_hash, uint32_t kml_bytes)
        : integrity_(io, hash), stream_(window, capacity, fingerprints, slots, kml_hash, kml_bytes, binding.bytes, binding.count),
          expected_(binding)
    {
        const auto* self = reinterpret_cast<const uint8_t*>(this);
        if (!stream_.admitted() || signingSpansOverlap(window, capacity, self, sizeof(*this)) ||
            signingSpansOverlap(reinterpret_cast<const uint8_t*>(fingerprints), kMaxTrackAttachments * sizeof(*fingerprints), self, sizeof(*this)) ||
            signingSpansOverlap(kml_hash.data, kml_hash.size, self, sizeof(*this)) ||
            signingSpansOverlap(binding.hash.data, binding.hash.size, self, sizeof(*this)) ||
            signingSpansOverlap(binding.hash.data, binding.hash.size, window, capacity) ||
            signingSpansOverlap(binding.hash.data, binding.hash.size, reinterpret_cast<const uint8_t*>(fingerprints), kMaxTrackAttachments * sizeof(*fingerprints)))
            result_ = AttachmentIndexFileResult::Invalid;
    }
    AttachmentIndexFileResult begin(const char* path)
    {
        if (result_ != AttachmentIndexFileResult::Idle) return AttachmentIndexFileResult::Busy;
        if (!expected_.hash.data || expected_.hash.size != 32) return result_ = AttachmentIndexFileResult::Invalid;
        const auto started = integrity_.begin(path, expected_.bytes, expected_.hash.data);
        if (started != FileIntegrityResult::Pending) return result_ = AttachmentIndexFileResult::Invalid;
        return result_ = AttachmentIndexFileResult::Pending;
    }
    AttachmentIndexFileResult step()
    {
        if (result_ != AttachmentIndexFileResult::Pending && result_ != AttachmentIndexFileResult::Entry) return result_;
        if (stream_.entry()) return result_ = AttachmentIndexFileResult::Entry;
        if (chunk_position_ < chunk_size_)
        {
            size_t consumed = 0;
            const auto parsed = stream_.feed(chunk_ + chunk_position_, chunk_size_ - chunk_position_, consumed);
            chunk_position_ += consumed;
            if (parsed == AttachmentStreamResult::Invalid) return stop(AttachmentIndexFileResult::Invalid);
            if (parsed == AttachmentStreamResult::Entry) return result_ = AttachmentIndexFileResult::Entry;
            return result_ = AttachmentIndexFileResult::Pending;
        }
        const auto before = integrity_.checkedBytes();
        const auto checked = integrity_.step(chunk_, sizeof(chunk_));
        chunk_size_ = integrity_.checkedBytes() - before;
        chunk_position_ = 0;
        if (checked == FileIntegrityResult::Pending) return result_ = AttachmentIndexFileResult::Pending;
        if (checked == FileIntegrityResult::Ready)
            return result_ = stream_.finish() ? AttachmentIndexFileResult::Ready : AttachmentIndexFileResult::Invalid;
        return result_ = checked == FileIntegrityResult::HashMismatch ? AttachmentIndexFileResult::HashMismatch : checked == FileIntegrityResult::Invalid ? AttachmentIndexFileResult::Invalid
                                                                                                                                                          : AttachmentIndexFileResult::StorageFailure;
    }
    const AttachmentEntryView* entry() const { return result_ == AttachmentIndexFileResult::Entry ? stream_.entry() : nullptr; }
    bool advance()
    {
        if (result_ != AttachmentIndexFileResult::Entry || !stream_.advance()) return false;
        result_ = AttachmentIndexFileResult::Pending;
        return true;
    }
    AttachmentIndexFileResult cancel()
    {
        if (result_ == AttachmentIndexFileResult::Pending || result_ == AttachmentIndexFileResult::Entry) return stop(AttachmentIndexFileResult::Cancelled);
        return result_;
    }
    bool ready() const { return result_ == AttachmentIndexFileResult::Ready; }
    bool matchesSource(const char* path, const AttachmentIndexBinding& binding) const
    {
        return ready() && binding.count == expected_.count && binding.hash.size == 32 && integrity_.matchesSource(path, binding.bytes, binding.hash.data);
    }
    uint32_t packageBytes() const { return stream_.packageBytes(); }

  private:
    AttachmentIndexFileResult stop(AttachmentIndexFileResult result)
    {
        integrity_.cancel();
        result_ = result;
        return result_;
    }
    FileIntegrityCheck<Io, Hash> integrity_;
    AttachmentIndexStream stream_;
    AttachmentIndexBinding expected_;
    uint8_t chunk_[320]{};
    size_t chunk_size_ = 0, chunk_position_ = 0;
    AttachmentIndexFileResult result_ = AttachmentIndexFileResult::Idle;
};
} // namespace track_sharing
