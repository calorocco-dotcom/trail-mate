#pragma once
#include "track_sharing/object_head.h"
#include <initializer_list>
namespace track_sharing
{
enum class HeadStoreResult : uint8_t
{
    Ready,
    Missing,
    Corrupt,
    StorageFailure,
    Conflict,
    Invalid
};
// Borrowed canonical paths and metadata. Caller owns media/exclusive object
// lease and signed-manifest evidence. Corrupt slots never authorize rollback.
template <class Io>
class ObjectHeadStore
{
  public:
    ObjectHeadStore(Io& io, const char* a, const char* b) : io_(io), a_(a), b_(b) {}
    HeadStoreResult load(ObjectHead& output, ObjectHead& other, uint8_t* scratch, size_t capacity)
    {
        if (&output == &other || !valid(scratch, capacity)) return HeadStoreResult::Invalid;
        output = {};
        other = {};
        const auto first = read(a_, output, scratch), second = read(b_, other, scratch);
        const auto fail = [&](HeadStoreResult result)
        { output = {}; other = {}; return result; };
        if (first == HeadStoreResult::StorageFailure || second == HeadStoreResult::StorageFailure) return fail(HeadStoreResult::StorageFailure);
        if (first == HeadStoreResult::Missing && second == HeadStoreResult::Missing) return HeadStoreResult::Missing;
        if (first == HeadStoreResult::Corrupt || second == HeadStoreResult::Corrupt) return fail(HeadStoreResult::Corrupt);
        if (first == HeadStoreResult::Ready && !(output.sequence & 1)) return fail(HeadStoreResult::Corrupt);
        if (second == HeadStoreResult::Ready && (other.sequence & 1)) return fail(HeadStoreResult::Corrupt);
        if (first == HeadStoreResult::Ready && second == HeadStoreResult::Ready)
        {
            if (output.sequence > other.sequence)
            {
                if (!objectHeadAdvanceValid(other, output)) return fail(HeadStoreResult::Corrupt);
            }
            else
            {
                if (!objectHeadAdvanceValid(output, other)) return fail(HeadStoreResult::Corrupt);
                output = other;
            }
            return HeadStoreResult::Ready;
        }
        if (first != HeadStoreResult::Ready) return fail(HeadStoreResult::Corrupt);
        if (output.sequence != 1 || (output.phase == HeadPhase::Signed && output.revision != 1)) return fail(HeadStoreResult::Corrupt);
        return HeadStoreResult::Ready;
    }
    HeadStoreResult initialize(const ObjectHead& initial, ObjectHead& verify, uint8_t* scratch, size_t capacity)
    {
        if (!valid(scratch, capacity) || &initial == &verify || !objectHeadValid(initial) || initial.sequence != 1 || initial.revision != 1 || initial.phase != HeadPhase::Signed) return HeadStoreResult::Invalid;
        for (const auto path : {a_, b_})
        {
            const auto existing = read(path, verify, scratch);
            if (existing == HeadStoreResult::StorageFailure) return existing;
            if (existing != HeadStoreResult::Missing) return HeadStoreResult::Conflict;
        }
        return write(a_, initial, verify, scratch, capacity);
    }
    // Only a caller with authenticated author + directory receipt and durable
    // evidence may initialize recovered history; normal signing stays rev1-only.
    HeadStoreResult initializeCommitted(const ObjectHead& initial, ObjectHead& verify, uint8_t* scratch, size_t capacity)
    {
        if (!valid(scratch, capacity) || &initial == &verify || !objectHeadValid(initial) || initial.sequence != 1 || initial.phase != HeadPhase::Committed)
            return HeadStoreResult::Invalid;
        for (const auto path : {a_, b_})
        {
            const auto existing = read(path, verify, scratch);
            if (existing == HeadStoreResult::StorageFailure) return existing;
            if (existing != HeadStoreResult::Missing) return HeadStoreResult::Conflict;
        }
        return write(a_, initial, verify, scratch, capacity);
    }
    HeadStoreResult validateCurrent(const ObjectHead& current, ObjectHead& verify, uint8_t* scratch, size_t capacity)
    {
        if (!valid(scratch, capacity) || &current == &verify || !objectHeadValid(current)) return HeadStoreResult::Invalid;
        auto result = read(current.sequence & 1 ? a_ : b_, verify, scratch);
        if (result == HeadStoreResult::StorageFailure) return result;
        if (result != HeadStoreResult::Ready || !same(current, verify)) return HeadStoreResult::Conflict;
        result = read(current.sequence & 1 ? b_ : a_, verify, scratch);
        if (result == HeadStoreResult::StorageFailure) return result;
        if (result == HeadStoreResult::Corrupt) return result;
        if (result == HeadStoreResult::Missing && (current.sequence != 1 || (current.phase == HeadPhase::Signed && current.revision != 1))) return HeadStoreResult::Corrupt;
        if (result == HeadStoreResult::Ready && !objectHeadAdvanceValid(verify, current)) return HeadStoreResult::Conflict;
        return HeadStoreResult::Ready;
    }
    HeadStoreResult advance(const ObjectHead& current, const ObjectHead& next, ObjectHead& verify, uint8_t* scratch, size_t capacity)
    {
        if (&next == &verify || !objectHeadAdvanceValid(current, next)) return HeadStoreResult::Invalid;
        const auto validated = validateCurrent(current, verify, scratch, capacity);
        if (validated != HeadStoreResult::Ready) return validated;
        return write(next.sequence & 1 ? a_ : b_, next, verify, scratch, capacity);
    }
    // Caller has verified the committed signed manifest against the current
    // identity. Reuse its two borrowed records; no third record or heap buffer.
    HeadStoreResult rebindCommittedGeneration(ObjectHead& current, ObjectHead& other, uint32_t generation, uint8_t* scratch, size_t capacity)
    {
        if (!generation || current.phase != HeadPhase::Committed || current.sequence == UINT64_MAX)
            return HeadStoreResult::Invalid;
        const auto validated = validateCurrent(current, other, scratch, capacity);
        if (validated != HeadStoreResult::Ready || current.generation == generation) return validated;
        other = current;
        ++other.sequence;
        other.generation = generation;
        // write encodes other before reading the persisted result into current.
        return write(other.sequence & 1 ? a_ : b_, other, current, scratch, capacity);
    }

  private:
    bool valid(uint8_t* scratch, size_t capacity) const { return scratch && capacity >= kObjectHeadBytes && a_ && *a_ && b_ && *b_ && std::strcmp(a_, b_); }
    static bool same(const ObjectHead& a, const ObjectHead& b)
    {
        return a.sequence == b.sequence && a.generation == b.generation && a.revision == b.revision && a.phase == b.phase && a.archived == b.archived &&
               !std::memcmp(a.object, b.object, 32) && !std::memcmp(a.hash, b.hash, 32) && !std::memcmp(a.parent, b.parent, 32) && !std::memcmp(a.operation, b.operation, 16);
    }
    HeadStoreResult read(const char* path, ObjectHead& output, uint8_t* scratch)
    {
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? HeadStoreResult::Missing : HeadStoreResult::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size);
        const bool read_ok = sized && size == kObjectHeadBytes && io_.read(opened.handle, scratch, kObjectHeadBytes) == kObjectHeadBytes;
        const bool closed = io_.close(opened.handle);
        if (!sized || !closed || (size == kObjectHeadBytes && !read_ok)) return HeadStoreResult::StorageFailure;
        return read_ok && decodeObjectHead(scratch, kObjectHeadBytes, output) ? HeadStoreResult::Ready : HeadStoreResult::Corrupt;
    }
    HeadStoreResult write(const char* path, const ObjectHead& desired, ObjectHead& verify, uint8_t* scratch, size_t capacity)
    {
        if (!encodeObjectHead(desired, scratch, capacity)) return HeadStoreResult::Invalid;
        const auto opened = io_.open(path, Io::Mode::CreateTemporary);
        if (!opened.handle) return HeadStoreResult::StorageFailure;
        const bool written = io_.write(opened.handle, scratch, kObjectHeadBytes) == kObjectHeadBytes && io_.sync(opened.handle);
        const bool closed = io_.close(opened.handle);
        if (!written || !closed) return HeadStoreResult::StorageFailure;
        return read(path, verify, scratch) == HeadStoreResult::Ready && same(desired, verify) ? HeadStoreResult::Ready : HeadStoreResult::StorageFailure;
    }
    Io& io_;
    const char* a_;
    const char* b_;
};
} // namespace track_sharing
