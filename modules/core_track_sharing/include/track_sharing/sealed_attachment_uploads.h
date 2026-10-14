#pragma once
#include "track_sharing/attachment_package_file_check.h"
#include "track_sharing/upload_journal_store.h"
#include "track_sharing/upload_paths.h"

namespace track_sharing
{
// Packed upload IDs follow canonical index order, 16 bytes per entry. A zero
// ID explicitly omits a resource; it does not authorize a path lookup by URL.
// Platform freezes this mapping and identity in admitted PSRAM under a lease.
template <class Io>
class SealedAttachmentUploads
{
  public:
    SealedAttachmentUploads(Io& io, geocaching::ByteView ids, uint32_t generation, const uint8_t* author_hash)
        : io_(io), ids_(ids), generation_(generation), author_hash_(author_hash) {}
    AttachmentResolveResult index(const uint8_t* id, const AttachmentIndexBinding& binding, char* path, size_t capacity)
    {
        if (!binding.hash.data || binding.hash.size != 32 || !binding.count || binding.count > 64 || ids_.size != size_t(binding.count) * 16)
            return AttachmentResolveResult::Invalid;
        return load(id, UploadKind::AttachmentIndex, binding.bytes, binding.hash.data, path, capacity);
    }
    AttachmentResolveResult resolve(uint8_t ordinal, const AttachmentEntryView& entry, char* path, size_t capacity)
    {
        if (!ids_.data || ordinal >= ids_.size / 16 || ids_.size % 16 || ids_.size > 64 * 16 || !entry.hash.data || entry.hash.size != 32)
            return AttachmentResolveResult::Invalid;
        const auto* id = ids_.data + size_t(ordinal) * 16;
        uint8_t bits = 0;
        for (size_t i = 0; i < 16; ++i) bits |= id[i];
        if (!bits) return AttachmentResolveResult::Missing;
        return load(id, UploadKind::AttachmentResource, entry.bytes, entry.hash.data, path, capacity);
    }

  private:
    AttachmentResolveResult load(const uint8_t* id, UploadKind kind, uint32_t bytes, const uint8_t* hash, char* path, size_t capacity)
    {
        if (!id || !hash || !author_hash_ || !generation_ || !ids_.data || !path || capacity < 64 || !bytes || bytes > uploadByteLimit(kind))
            return AttachmentResolveResult::Invalid;
        if (!resolveUploadPath(id, 16, UploadFile::Candidate, path, capacity, kind) ||
            !resolveUploadPath(id, 16, UploadFile::JournalA, a_, sizeof(a_), kind) ||
            !resolveUploadPath(id, 16, UploadFile::JournalB, b_, sizeof(b_), kind)) return AttachmentResolveResult::Invalid;
        UploadJournalStore<Io> store(io_, a_, b_);
        const auto loaded = store.load(current_, verify_, scratch_, sizeof(scratch_));
        if (loaded == JournalStoreResult::StorageFailure) return AttachmentResolveResult::StorageFailure;
        // A supplied ID with missing/corrupt journals is invalid even when
        // optional. Only a zero ID means intentional absence.
        if (loaded != JournalStoreResult::Ready || !uploadJournalValid(current_) || current_.phase != UploadPhase::Sealed ||
            current_.generation != generation_ || current_.total != bytes || std::memcmp(current_.id, id, 16) ||
            std::memcmp(current_.author_hash, author_hash_, 32) || std::memcmp(current_.file_hash, hash, 32)) return AttachmentResolveResult::Invalid;
        return AttachmentResolveResult::Ready;
    }
    Io& io_;
    geocaching::ByteView ids_;
    uint32_t generation_;
    const uint8_t* author_hash_;
    UploadJournal current_, verify_;
    char a_[64]{}, b_[64]{};
    uint8_t scratch_[108]{};
};
} // namespace track_sharing
