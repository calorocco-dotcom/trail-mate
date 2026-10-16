#pragma once
#include "track_sharing/publication_directory_store.h"
#include "track_sharing/publication_slot_path.h"
namespace track_sharing
{
// Caller admits this metadata owner in PSRAM and holds a media lease. A Ready
// candidate is NOT authenticated: use current-author, manifest/blob and receipt
// validation before showing status or creating a network execution.
template <class Io>
class PublicationCandidateReader
{
  public:
    explicit PublicationCandidateReader(Io& io) : directories_(io, directory_path_), jobs_(io, a_, b_) {}
    PublicationStoreResult load(const PublicationSlotKey& key, uint8_t* scratch, size_t capacity)
    {
        ready_ = false;
        if (!scratch || capacity < kPublicationJournalBytes ||
            signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            signingSpansOverlap(reinterpret_cast<const uint8_t*>(&key), sizeof(key), reinterpret_cast<const uint8_t*>(this), sizeof(*this)))
            return PublicationStoreResult::Invalid;
        directory_ = {};
        current_ = {};
        other_ = {};
        if ((key.slot != ObjectFile::PublicationA && key.slot != ObjectFile::PublicationB) ||
            !resolveObjectPath({key.object, 32}, ObjectFile::PublicationDirectory, {key.operation, 16}, directory_path_, sizeof(directory_path_)) ||
            !resolveObjectPath({key.object, 32}, ObjectFile::PublicationA, {key.operation, 16}, a_, sizeof(a_)) ||
            !resolveObjectPath({key.object, 32}, ObjectFile::PublicationB, {key.operation, 16}, b_, sizeof(b_))) return PublicationStoreResult::Invalid;
        const auto fail = [&](PublicationStoreResult result)
        {directory_={};current_={};other_={};return result; };
        const auto pin = directories_.load(directory_, scratch, capacity);
        if (pin != PublicationStoreResult::Ready) return fail(pin);
        const auto journal = jobs_.load(current_, other_, scratch, capacity);
        if (journal != PublicationStoreResult::Ready) return fail(journal);
        if (std::memcmp(key.object, current_.object, 32) || std::memcmp(key.operation, current_.operation, 16) ||
            !publicationDirectoryMatches(directory_, current_)) return fail(PublicationStoreResult::Conflict);
        ready_ = true;
        return PublicationStoreResult::Ready;
    }
    const PublicationJournal* unverifiedJournal() const { return ready_ ? &current_ : nullptr; }
    const PublicationDirectory* unverifiedDirectory() const { return ready_ ? &directory_ : nullptr; }

  private:
    char directory_path_[kObjectPathCapacity]{}, a_[kObjectPathCapacity]{}, b_[kObjectPathCapacity]{};
    PublicationDirectory directory_;
    PublicationJournal current_, other_;
    PublicationDirectoryStore<Io> directories_;
    PublicationJournalStore<Io> jobs_;
    bool ready_ = false;
};
} // namespace track_sharing
