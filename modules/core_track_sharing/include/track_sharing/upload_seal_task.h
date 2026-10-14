#pragma once
#include "track_sharing/file_integrity.h"
#include "track_sharing/upload_journal_store.h"
namespace track_sharing
{
enum class UploadSealResult : uint8_t
{
    Idle,
    Pending,
    Ready,
    Invalid,
    Conflict,
    StorageFailure,
    HashMismatch,
    Cancelled
};
// Borrowed, single-use task. Platform retains exclusive immutable candidate
// lease, media-bound I/O and authenticated current identity throughout. Sealed
// proves file bytes only; XML/manifest/revision authorization still follows.
template <class Check, class Store>
class UploadSealTask
{
  public:
    UploadSealTask(Check& check, Store& store, UploadJournal& current, UploadJournal& verify,
                   const uint32_t& generation, const uint8_t* author)
        : check_(check), store_(store), current_(current), verify_(verify), generation_(generation), author_(author) {}
    UploadSealResult begin(const char* path, uint8_t* scratch, size_t capacity)
    {
        if (result_ != UploadSealResult::Idle) return UploadSealResult::Conflict;
        if (!path || !*path || !scratch || capacity < 108 || &current_ == &verify_ || !uploadJournalValid(current_)) return UploadSealResult::Invalid;
        if (!identity() || current_.durable_offset != current_.total ||
            (current_.phase != UploadPhase::Receiving && current_.phase != UploadPhase::Checking)) return UploadSealResult::Conflict;
        const auto journal = current_.phase == UploadPhase::Receiving ? store_.advance(current_, current_.total, UploadPhase::Checking, verify_, scratch, capacity)
                                                                      : store_.validateCurrent(current_, verify_, scratch, capacity);
        if (journal != JournalStoreResult::Ready) return map(journal);
        if (current_.phase == UploadPhase::Receiving) current_ = verify_;
        if (check_.begin(path, current_.total, current_.file_hash) != FileIntegrityResult::Pending) return result_ = UploadSealResult::StorageFailure;
        return result_ = UploadSealResult::Pending;
    }
    UploadSealResult step(uint8_t* scratch, size_t capacity)
    {
        if (result_ != UploadSealResult::Pending) return result_;
        if (!identity())
        {
            check_.cancel();
            return result_ = UploadSealResult::Conflict;
        }
        const auto status = check_.step(scratch, capacity);
        if (status == FileIntegrityResult::Pending) return result_;
        if (status != FileIntegrityResult::Ready && status != FileIntegrityResult::HashMismatch) return result_ = UploadSealResult::StorageFailure;
        const auto journal = store_.advance(current_, current_.total, status == FileIntegrityResult::Ready ? UploadPhase::Sealed : UploadPhase::Failed, verify_, scratch, capacity);
        if (journal != JournalStoreResult::Ready) return result_ = map(journal);
        current_ = verify_;
        return result_ = status == FileIntegrityResult::Ready ? UploadSealResult::Ready : UploadSealResult::HashMismatch;
    }
    UploadSealResult cancel()
    {
        if (result_ == UploadSealResult::Pending)
        {
            check_.cancel();
            result_ = UploadSealResult::Cancelled;
        }
        return result_;
    }
    UploadSealResult result() const { return result_; }

  private:
    bool identity() const { return author_ && generation_ && generation_ == current_.generation && !std::memcmp(author_, current_.author_hash, 32); }
    static UploadSealResult map(JournalStoreResult value) { return value == JournalStoreResult::Conflict ? UploadSealResult::Conflict : value == JournalStoreResult::Invalid ? UploadSealResult::Invalid
                                                                                                                                                                             : UploadSealResult::StorageFailure; }
    Check& check_;
    Store& store_;
    UploadJournal& current_;
    UploadJournal& verify_;
    const uint32_t& generation_;
    const uint8_t* author_;
    UploadSealResult result_ = UploadSealResult::Idle;
};
} // namespace track_sharing
