#pragma once
#include "track_sharing/upload_journal_store.h"
#include <algorithm>
namespace track_sharing
{
// Caller authenticates the device identity, owns the exclusive transaction
// lease, and supplies stable confined paths and media-bound I/O. Candidate
// already exists. No file handle or byte buffer survives append().
template <class Io, class Store>
class UploadChunkWriter
{
  public:
    // Live identity fields are supplied from current device state, never from
    // the recovered journal. Caller refreshes them before every serialized call.
    UploadChunkWriter(Io& io, Store& store, UploadJournal& current, UploadJournal& verify, const char* path,
                      const uint32_t& generation, const uint8_t* author_hash)
        : io_(io), store_(store), current_(current), verify_(verify), path_(path), generation_(generation), author_hash_(author_hash) {}
    JournalStoreResult append(const uint8_t* id, uint32_t offset, const uint8_t* bytes, size_t length,
                              uint8_t* scratch, size_t capacity)
    {
        if (!id || !bytes || !scratch || capacity < 128 || !length || length > 320 || !path_ || !*path_ ||
            &current_ == &verify_ || !uploadJournalValid(current_)) return JournalStoreResult::Invalid;
        const auto input = reinterpret_cast<uintptr_t>(bytes), workspace = reinterpret_cast<uintptr_t>(scratch);
        if (input <= workspace ? workspace - input < length : input - workspace < 128) return JournalStoreResult::Invalid;
        if (!author_hash_ || !generation_ || generation_ != current_.generation || std::memcmp(author_hash_, current_.author_hash, 32)) return JournalStoreResult::Conflict;
        if (std::memcmp(id, current_.id, 16) || current_.phase != UploadPhase::Receiving ||
            offset > current_.durable_offset || offset > current_.total || length > current_.total - offset) return JournalStoreResult::Conflict;
        const bool duplicate = offset < current_.durable_offset;
        if (duplicate && length > current_.durable_offset - offset) return JournalStoreResult::Conflict;
        const auto checked = store_.validateCurrent(current_, verify_, scratch, capacity);
        if (checked != JournalStoreResult::Ready) return checked;
        const auto opened = io_.open(path_, duplicate ? Io::Mode::Read : Io::Mode::Update);
        if (!opened.handle) return JournalStoreResult::StorageFailure;
        uint64_t size = 0;
        bool ok = io_.size(opened.handle, size) && size >= current_.durable_offset && size <= current_.total && io_.seek(opened.handle, offset);
        bool equal = true;
        if (duplicate)
        {
            for (size_t consumed = 0; ok && equal && consumed < length;)
            {
                const size_t count = std::min(size_t(128), length - consumed);
                ok = io_.read(opened.handle, scratch, count) == count;
                if (ok) equal = !std::memcmp(scratch, bytes + consumed, count);
                consumed += count;
            }
        }
        else if (ok) ok = io_.write(opened.handle, bytes, length) == length && io_.sync(opened.handle);
        const bool closed = io_.close(opened.handle);
        if (!ok || !closed) return JournalStoreResult::StorageFailure;
        if (!equal) return JournalStoreResult::Conflict;
        if (duplicate) return JournalStoreResult::Ready;
        const auto result = store_.advance(current_, offset + uint32_t(length), UploadPhase::Receiving, verify_, scratch, capacity);
        if (result == JournalStoreResult::Ready) current_ = verify_;
        return result;
    }

  private:
    Io& io_;
    Store& store_;
    UploadJournal& current_;
    UploadJournal& verify_;
    const char* path_;
    const uint32_t& generation_;
    const uint8_t* author_hash_;
};
} // namespace track_sharing
