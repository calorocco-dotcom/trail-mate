#pragma once
#include "track_sharing/upload_journal.h"
#include <initializer_list>
namespace track_sharing
{
enum class JournalStoreResult : uint8_t
{
    Ready,
    Missing,
    Corrupt,
    StorageFailure,
    Conflict,
    Invalid
};
// Caller owns exclusive transaction lease, immutable canonical distinct paths,
// media-bound I/O, two metadata objects and a 108-byte scratch buffer. No file
// handles or automatic metadata/buffers survive a call. CRC is not file proof.
template <class Io>
class UploadJournalStore
{
  public:
    UploadJournalStore(Io& io, const char* a, const char* b) : io_(io), a_(a), b_(b) {}
    JournalStoreResult initialize(const UploadJournal& initial, UploadJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (!paths() || !scratch || capacity < 108 || &initial == &verify || !uploadJournalValid(initial) ||
            initial.sequence != 1 || initial.durable_offset || initial.phase != UploadPhase::Receiving) return JournalStoreResult::Invalid;
        for (const auto path : {a_, b_})
        {
            const auto existing = read(path, verify, scratch);
            if (existing == JournalStoreResult::StorageFailure) return existing;
            if (existing != JournalStoreResult::Missing) return JournalStoreResult::Conflict;
        }
        if (!encodeUploadJournal(initial, scratch, capacity)) return JournalStoreResult::Invalid;
        const auto opened = io_.open(a_, Io::Mode::CreateTemporary);
        if (!opened.handle) return JournalStoreResult::StorageFailure;
        const bool written = io_.write(opened.handle, scratch, 108) == 108 && io_.sync(opened.handle);
        const bool closed = io_.close(opened.handle);
        if (!written || !closed) return JournalStoreResult::StorageFailure;
        if (read(a_, verify, scratch) != JournalStoreResult::Ready || !same(initial, verify)) return JournalStoreResult::StorageFailure;
        return JournalStoreResult::Ready;
    }
    // Check before modifying candidate bytes; checking only at commit is too
    // late when a stale offset would overwrite already acknowledged content.
    JournalStoreResult validateCurrent(const UploadJournal& current, UploadJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (!paths() || !scratch || capacity < 108 || &current == &verify || !uploadJournalValid(current)) return JournalStoreResult::Invalid;
        const auto existing = read(current.sequence & 1 ? a_ : b_, verify, scratch);
        if (existing == JournalStoreResult::StorageFailure) return existing;
        if (existing != JournalStoreResult::Ready || !same(current, verify)) return JournalStoreResult::Conflict;
        const auto other = read(current.sequence & 1 ? b_ : a_, verify, scratch);
        if (other == JournalStoreResult::StorageFailure) return other;
        if (other == JournalStoreResult::Ready && (!binding(current, verify) || verify.sequence >= current.sequence)) return JournalStoreResult::Conflict;
        return JournalStoreResult::Ready;
    }
    // Advance without allocating a third metadata object. Current remains
    // unchanged; caller adopts verify only after Ready. Exclusive lease required.
    JournalStoreResult advance(const UploadJournal& current, uint32_t offset, UploadPhase phase,
                               UploadJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (!paths() || !scratch || capacity < 108 || &current == &verify || !uploadJournalValid(current)) return JournalStoreResult::Invalid;
        if (current.sequence == UINT64_MAX || offset < current.durable_offset || offset > current.total ||
            !transition(current.phase, phase) || ((phase == UploadPhase::Checking || phase == UploadPhase::Sealed) && offset != current.total)) return JournalStoreResult::Conflict;
        const auto checked = validateCurrent(current, verify, scratch, capacity);
        if (checked != JournalStoreResult::Ready) return checked;
        if (!encodeUploadJournal(current, scratch, capacity)) return JournalStoreResult::Invalid;
        for (unsigned i = 0; i < 4; ++i) scratch[93 + i] = uint8_t(offset >> (8 * i));
        scratch[97] = static_cast<uint8_t>(phase);
        const uint64_t sequence = current.sequence + 1;
        for (unsigned i = 0; i < 8; ++i) scratch[98 + i] = uint8_t(sequence >> (8 * i));
        const auto crc = uploadJournalCrc(scratch, 106);
        scratch[106] = uint8_t(crc);
        scratch[107] = uint8_t(crc >> 8);
        const char* target = sequence & 1 ? a_ : b_;
        const auto opened = io_.open(target, Io::Mode::CreateTemporary);
        if (!opened.handle) return JournalStoreResult::StorageFailure;
        const bool written = io_.write(opened.handle, scratch, 108) == 108 && io_.sync(opened.handle);
        const bool closed = io_.close(opened.handle);
        if (!written || !closed) return JournalStoreResult::StorageFailure;
        if (read(target, verify, scratch) != JournalStoreResult::Ready || !binding(current, verify) ||
            verify.sequence != sequence || verify.durable_offset != offset || verify.phase != phase) return JournalStoreResult::StorageFailure;
        return JournalStoreResult::Ready;
    }
    JournalStoreResult load(UploadJournal& output, UploadJournal& candidate, uint8_t* scratch, size_t capacity)
    {
        output = {};
        if (&output == &candidate || !paths() || !scratch || capacity < 108) return JournalStoreResult::Invalid;
        auto first = read(a_, output, scratch), second = read(b_, candidate, scratch);
        if (first == JournalStoreResult::Ready && !(output.sequence & 1)) first = JournalStoreResult::Corrupt;
        if (second == JournalStoreResult::Ready && (candidate.sequence & 1)) second = JournalStoreResult::Corrupt;
        if (first == JournalStoreResult::StorageFailure || second == JournalStoreResult::StorageFailure)
        {
            output = {};
            return JournalStoreResult::StorageFailure;
        }
        if (first == JournalStoreResult::Ready && second == JournalStoreResult::Ready)
        {
            const auto& newer = output.sequence > candidate.sequence ? output : candidate;
            const auto& older = output.sequence > candidate.sequence ? candidate : output;
            if (!binding(newer, older) || newer.durable_offset < older.durable_offset || static_cast<uint8_t>(newer.phase) < static_cast<uint8_t>(older.phase))
            {
                output = {};
                return JournalStoreResult::Conflict;
            }
            if (candidate.sequence > output.sequence) output = candidate;
            return JournalStoreResult::Ready;
        }
        if (second == JournalStoreResult::Ready)
        {
            output = candidate;
            return JournalStoreResult::Ready;
        }
        if (first == JournalStoreResult::Ready) return JournalStoreResult::Ready;
        output = {};
        return first == JournalStoreResult::Missing && second == JournalStoreResult::Missing ? JournalStoreResult::Missing : JournalStoreResult::Corrupt;
    }
    JournalStoreResult commit(const UploadJournal& current, const UploadJournal& desired, UploadJournal& verify, uint8_t* scratch, size_t capacity)
    {
        if (!paths() || !scratch || capacity < 108 || &verify == &current || &verify == &desired || !uploadJournalValid(desired)) return JournalStoreResult::Invalid;
        if (current.sequence)
        {
            if (!uploadJournalValid(current) || current.sequence == UINT64_MAX || desired.sequence != current.sequence + 1 || !binding(current, desired) || desired.durable_offset < current.durable_offset || !transition(current.phase, desired.phase)) return JournalStoreResult::Conflict;
            return advance(current, desired.durable_offset, desired.phase, verify, scratch, capacity);
        }
        else
        {
            return initialize(desired, verify, scratch, capacity);
        }
    }

  private:
    bool paths() const
    {
        if (!a_ || !b_ || !*a_ || !*b_) return false;
        for (size_t i = 0;; ++i)
        {
            const auto lower = [](char c)
            { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
            if (lower(a_[i]) != lower(b_[i])) return true;
            if (!a_[i]) return false;
        }
    }
    JournalStoreResult read(const char* path, UploadJournal& output, uint8_t* scratch)
    {
        output = {};
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? JournalStoreResult::Missing : JournalStoreResult::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size);
        const bool read = sized && size == 108 && io_.seek(opened.handle, 0) && io_.read(opened.handle, scratch, 108) == 108;
        const bool closed = io_.close(opened.handle);
        if (!closed || !sized) return JournalStoreResult::StorageFailure;
        if (size != 108) return JournalStoreResult::Corrupt;
        if (!read) return JournalStoreResult::StorageFailure;
        return decodeUploadJournal(scratch, 108, output) ? JournalStoreResult::Ready : JournalStoreResult::Corrupt;
    }
    static bool binding(const UploadJournal& a, const UploadJournal& b) { return a.generation == b.generation && a.total == b.total && !std::memcmp(a.id, b.id, 16) && !std::memcmp(a.author_hash, b.author_hash, 32) && !std::memcmp(a.file_hash, b.file_hash, 32); }
    static bool same(const UploadJournal& a, const UploadJournal& b) { return binding(a, b) && a.sequence == b.sequence && a.durable_offset == b.durable_offset && a.phase == b.phase; }
    static bool transition(UploadPhase a, UploadPhase b) { return a == UploadPhase::Receiving ? (b == a || b == UploadPhase::Checking || b == UploadPhase::Failed) : a == UploadPhase::Checking ? (b == a || b == UploadPhase::Sealed || b == UploadPhase::Failed)
                                                                                                                                                                                                : b == a; }
    Io& io_;
    const char* a_;
    const char* b_;
};
} // namespace track_sharing
