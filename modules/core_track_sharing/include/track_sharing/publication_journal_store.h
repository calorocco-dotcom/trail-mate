#pragma once
#include "track_sharing/publication_journal.h"
#include <initializer_list>
namespace track_sharing
{
enum class PublicationStoreResult : uint8_t { Ready, Missing, Corrupt, StorageFailure, Conflict, Invalid };
// Borrowed canonical paths and metadata. Caller owns media/exclusive object
// lease and verified publication evidence. Corrupt slots never authorize rollback.
template <class Io>
class PublicationJournalStore
{
  public:
    PublicationJournalStore(Io& io, const char* a, const char* b) : io_(io), a_(a), b_(b) {}
    PublicationStoreResult load(PublicationJournal& output, PublicationJournal& other, uint8_t* scratch, size_t capacity)
    {
        if (&output == &other || !valid(scratch, capacity)) return PublicationStoreResult::Invalid;
        output = {}; other = {};
        const auto first = read(a_, output, scratch), second = read(b_, other, scratch);
        const auto fail = [&](PublicationStoreResult result) { output = {}; other = {}; return result; };
        if (first == PublicationStoreResult::StorageFailure || second == PublicationStoreResult::StorageFailure) return fail(PublicationStoreResult::StorageFailure);
        if (first == PublicationStoreResult::Missing && second == PublicationStoreResult::Missing) return PublicationStoreResult::Missing;
        if (first == PublicationStoreResult::Corrupt || second == PublicationStoreResult::Corrupt) return fail(PublicationStoreResult::Corrupt);
        if (first == PublicationStoreResult::Ready && !(output.sequence & 1)) return fail(PublicationStoreResult::Corrupt);
        if (second == PublicationStoreResult::Ready && (other.sequence & 1)) return fail(PublicationStoreResult::Corrupt);
        if (first == PublicationStoreResult::Ready && second == PublicationStoreResult::Ready)
        {
            if (output.sequence > other.sequence)
            { if (!publicationAdvanceValid(other, output)) return fail(PublicationStoreResult::Corrupt); }
            else
            { if (!publicationAdvanceValid(output, other)) return fail(PublicationStoreResult::Corrupt); output = other; }
            return PublicationStoreResult::Ready;
        }
        if (first != PublicationStoreResult::Ready) return fail(PublicationStoreResult::Corrupt);
        if (output.sequence != 1 || output.phase != PublicationPhase::Accepted) return fail(PublicationStoreResult::Corrupt);
        return PublicationStoreResult::Ready;
    }
    PublicationStoreResult initialize(const PublicationJournal& initial, PublicationJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (!valid(scratch, capacity) || &initial == &verify || !publicationJournalValid(initial) || initial.sequence != 1 || initial.phase != PublicationPhase::Accepted) return PublicationStoreResult::Invalid;
        for (const auto path : {a_, b_})
        {
            const auto existing = read(path, verify, scratch);
            if (existing == PublicationStoreResult::StorageFailure) return existing;
            if (existing != PublicationStoreResult::Missing) return PublicationStoreResult::Conflict;
        }
        return write(a_, initial, verify, scratch, capacity);
    }
    PublicationStoreResult validateCurrent(const PublicationJournal& current, PublicationJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (!valid(scratch, capacity) || &current == &verify || !publicationJournalValid(current)) return PublicationStoreResult::Invalid;
        auto result = read(current.sequence & 1 ? a_ : b_, verify, scratch);
        if (result == PublicationStoreResult::StorageFailure) return result;
        if (result != PublicationStoreResult::Ready || !same(current, verify)) return PublicationStoreResult::Conflict;
        result = read(current.sequence & 1 ? b_ : a_, verify, scratch);
        if (result == PublicationStoreResult::StorageFailure) return result;
        if (result == PublicationStoreResult::Corrupt) return result;
        if (result == PublicationStoreResult::Missing && (current.sequence != 1 || current.phase != PublicationPhase::Accepted)) return PublicationStoreResult::Corrupt;
        if (result == PublicationStoreResult::Ready && !publicationAdvanceValid(verify, current)) return PublicationStoreResult::Conflict;
        return PublicationStoreResult::Ready;
    }
    PublicationStoreResult advance(const PublicationJournal& current, const PublicationJournal& next, PublicationJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (&next == &verify || !publicationAdvanceValid(current, next)) return PublicationStoreResult::Invalid;
        const auto validated = validateCurrent(current, verify, scratch, capacity);
        if (validated != PublicationStoreResult::Ready) return validated;
        return write(next.sequence & 1 ? a_ : b_, next, verify, scratch, capacity);
    }
  private:
    bool valid(uint8_t* scratch, size_t capacity) const { return scratch && capacity >= kPublicationJournalBytes && a_ && *a_ && b_ && *b_ && std::strcmp(a_, b_); }
    static bool same(const PublicationJournal& a, const PublicationJournal& b)
    {
        return a.sequence == b.sequence && a.generation == b.generation && a.revision == b.revision && a.phase == b.phase &&
               a.file_bytes == b.file_bytes && a.offset == b.offset &&
               !std::memcmp(a.operation,b.operation,16) && !std::memcmp(a.object,b.object,32) &&
               !std::memcmp(a.revision_hash,b.revision_hash,32) && !std::memcmp(a.directory,b.directory,16) &&
               !std::memcmp(a.directory_epoch,b.directory_epoch,16) && !std::memcmp(a.file_hash,b.file_hash,32) && !std::memcmp(a.author_hash,b.author_hash,32);
    }
    PublicationStoreResult read(const char* path, PublicationJournal& output, uint8_t* scratch)
    {
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? PublicationStoreResult::Missing : PublicationStoreResult::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size);
        const bool read_ok = sized && size == kPublicationJournalBytes && io_.read(opened.handle, scratch, kPublicationJournalBytes) == kPublicationJournalBytes;
        const bool closed = io_.close(opened.handle);
        if (!sized || !closed || (size == kPublicationJournalBytes && !read_ok)) return PublicationStoreResult::StorageFailure;
        return read_ok && decodePublicationJournal(scratch, kPublicationJournalBytes, output) ? PublicationStoreResult::Ready : PublicationStoreResult::Corrupt;
    }
    PublicationStoreResult write(const char* path, const PublicationJournal& desired, PublicationJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (!encodePublicationJournal(desired, scratch, capacity)) return PublicationStoreResult::Invalid;
        const auto opened = io_.open(path, Io::Mode::CreateTemporary);
        if (!opened.handle) return PublicationStoreResult::StorageFailure;
        const bool written = io_.write(opened.handle, scratch, kPublicationJournalBytes) == kPublicationJournalBytes && io_.sync(opened.handle);
        const bool closed = io_.close(opened.handle);
        if (!written || !closed) return PublicationStoreResult::StorageFailure;
        return read(path, verify, scratch) == PublicationStoreResult::Ready && same(desired, verify) ? PublicationStoreResult::Ready : PublicationStoreResult::StorageFailure;
    }
    Io& io_;
    const char* a_;
    const char* b_;
};
} // namespace track_sharing
