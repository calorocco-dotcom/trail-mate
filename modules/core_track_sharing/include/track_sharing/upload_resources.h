#pragma once
#include "track_sharing/upload_chunk_writer.h"
#include "track_sharing/upload_paths.h"
#include "track_sharing/upload_seal_task.h"
namespace track_sharing
{
// Platform admits this object in transient PSRAM, plus the borrowed I/O driver
// and hash context. No allocation here. Candidate is created exclusively by
// platform after quota/absence checks, before initialize(). A new instance is
// required after cancellation/failure; persisted Checking is revalidated.
template <class Io, class Hash>
class UploadResources
{
  public:
    UploadResources(Io& io, Hash& hash, const uint8_t* id, const uint32_t& generation, const uint8_t* author, UploadKind kind = UploadKind::Kml)
        : io_(io), generation_(generation), author_(author), store_(io, a_, b_), writer_(io, store_, current_, verify_, candidate_, generation, author),
          check_(io, hash), seal_(check_, store_, current_, verify_, generation, author), kind_(kind)
    {
        paths_ = resolveUploadPath(id, 16, UploadFile::Candidate, candidate_, sizeof(candidate_), kind_) &&
                 resolveUploadPath(id, 16, UploadFile::JournalA, a_, sizeof(a_), kind_) && resolveUploadPath(id, 16, UploadFile::JournalB, b_, sizeof(b_), kind_);
        if (paths_) std::memcpy(id_, id, 16);
    }
    UploadResources(const UploadResources&) = delete;
    UploadResources& operator=(const UploadResources&) = delete;
    const char* candidatePath() const { return paths_ ? candidate_ : nullptr; }
    const UploadJournal& metadata() const { return current_; }
    JournalStoreResult recover(uint8_t* scratch, size_t capacity)
    {
        if (!paths_ || started_) return JournalStoreResult::Invalid;
        const auto result = store_.load(current_, verify_, scratch, capacity);
        if (result != JournalStoreResult::Ready)
        {
            bound_ = false;
            return result;
        }
        bound_ = current_.total <= uploadByteLimit(kind_) && author_ && generation_ && generation_ == current_.generation && !std::memcmp(author_, current_.author_hash, 32) && !std::memcmp(id_, current_.id, 16);
        if (!bound_)
        {
            current_ = {};
            return JournalStoreResult::Conflict;
        }
        return JournalStoreResult::Ready;
    }
    JournalStoreResult initialize(uint32_t total, const uint8_t* hash, uint8_t* scratch, size_t capacity)
    {
        if (!paths_ || bound_ || started_ || !author_ || !generation_ || !hash || !total || total > uploadByteLimit(kind_) || !scratch || capacity < 108) return JournalStoreResult::Invalid;
        // Existing candidate must be empty; do not truncate files implicitly.
        const auto opened = io_.open(candidate_, Io::Mode::Read);
        if (!opened.handle) return JournalStoreResult::StorageFailure;
        uint64_t bytes = 0;
        const bool sized = io_.size(opened.handle, bytes), closed = io_.close(opened.handle);
        if (!sized || !closed) return JournalStoreResult::StorageFailure;
        if (bytes) return JournalStoreResult::Conflict;
        current_ = {};
        std::memcpy(current_.id, id_, 16);
        std::memcpy(current_.author_hash, author_, 32);
        std::memcpy(current_.file_hash, hash, 32);
        current_.sequence = 1;
        current_.generation = generation_;
        current_.total = total;
        const auto result = store_.initialize(current_, verify_, scratch, capacity);
        bound_ = result == JournalStoreResult::Ready;
        if (!bound_) current_ = {};
        return result;
    }
    JournalStoreResult append(const uint8_t* id, uint32_t offset, const uint8_t* bytes, size_t length, uint8_t* scratch, size_t capacity)
    {
        if (!bound_ || started_) return JournalStoreResult::Conflict;
        return writer_.append(id, offset, bytes, length, scratch, capacity);
    }
    UploadSealResult beginSeal(uint8_t* scratch, size_t capacity)
    {
        if (!bound_ || started_) return UploadSealResult::Conflict;
        const auto result = seal_.begin(candidate_, scratch, capacity);
        started_ = result == UploadSealResult::Pending;
        return result;
    }
    UploadSealResult step(uint8_t* scratch, size_t capacity) { return seal_.step(scratch, capacity); }
    UploadSealResult cancel() { return seal_.cancel(); }
    bool sealingStarted() const { return started_; }
    UploadSealResult sealResult() const { return seal_.result(); }

  private:
    Io& io_;
    const uint32_t& generation_;
    const uint8_t* author_;
    char candidate_[64]{}, a_[64]{}, b_[64]{};
    uint8_t id_[16]{};
    UploadJournal current_, verify_;
    UploadJournalStore<Io> store_;
    UploadChunkWriter<Io, UploadJournalStore<Io>> writer_;
    FileIntegrityCheck<Io, Hash> check_;
    UploadSealTask<FileIntegrityCheck<Io, Hash>, UploadJournalStore<Io>> seal_;
    UploadKind kind_;
    bool paths_ = false, bound_ = false, started_ = false;
};
} // namespace track_sharing
