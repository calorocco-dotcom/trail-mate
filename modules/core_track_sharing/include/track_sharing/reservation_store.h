#pragma once
#include "track_sharing/revision_reservation.h"
namespace track_sharing
{
enum class ReservationStoreResult : uint8_t
{
    Ready,
    Missing,
    Corrupt,
    StorageFailure,
    Conflict,
    Invalid
};
// Exclusive lease and canonical distinct paths are caller-owned. Two borrowed
// records/scratch only; phase proof and current-head arbitration remain external.
template <class Io>
class ReservationStore
{
  public:
    using Result = ReservationStoreResult;
    ReservationStore(Io& io, const char* a, const char* b) : io_(io), a_(a), b_(b) {}
    Result load(RevisionReservation& current, RevisionReservation& other, uint8_t* scratch, size_t capacity)
    {
        if (!inputs(current, other, scratch, capacity)) return Result::Invalid;
        const auto first = read(a_, current, scratch), second = read(b_, other, scratch);
        if (first == Result::StorageFailure || second == Result::StorageFailure)
        {
            current = {};
            return Result::StorageFailure;
        }
        const bool good_a = first == Result::Ready && (current.sequence & 1), good_b = second == Result::Ready && !(other.sequence & 1);
        if (good_a && good_b)
        {
            const auto& newer = current.sequence > other.sequence ? current : other;
            const auto& older = current.sequence > other.sequence ? other : current;
            if (!reservationAdvanceValid(older, newer))
            {
                current = {};
                return Result::Conflict;
            }
            if (other.sequence > current.sequence) current = other;
            return Result::Ready;
        }
        if (good_b)
        {
            current = other;
            return Result::Ready;
        }
        if (good_a) return Result::Ready;
        current = {};
        return first == Result::Missing && second == Result::Missing ? Result::Missing : Result::Corrupt;
    }
    Result initialize(const RevisionReservation& initial, RevisionReservation& verify, uint8_t* scratch, size_t capacity)
    {
        if (!inputs(initial, verify, scratch, capacity) || !reservationValid(initial) || initial.sequence != 1 || initial.phase != ReservationPhase::Reserved) return Result::Invalid;
        const auto first = read(a_, verify, scratch);
        if (first == Result::StorageFailure) return first;
        if (first != Result::Missing) return Result::Conflict;
        const auto second = read(b_, verify, scratch);
        if (second == Result::StorageFailure) return second;
        if (second != Result::Missing) return Result::Conflict;
        if (!encodeReservation(initial, scratch, capacity)) return Result::Invalid;
        const auto result = persist(1, verify, scratch);
        return result == Result::Ready && !same(initial, verify) ? Result::StorageFailure : result;
    }
    Result validateCurrent(const RevisionReservation& current, RevisionReservation& verify, uint8_t* scratch, size_t capacity)
    {
        if (!inputs(current, verify, scratch, capacity) || !reservationValid(current)) return Result::Invalid;
        const auto existing = read(current.sequence & 1 ? a_ : b_, verify, scratch);
        if (existing == Result::StorageFailure) return existing;
        if (existing != Result::Ready || !same(current, verify)) return Result::Conflict;
        const auto other = read(current.sequence & 1 ? b_ : a_, verify, scratch);
        if (other == Result::StorageFailure) return other;
        return other == Result::Ready && (!binding(current, verify) || verify.sequence >= current.sequence) ? Result::Conflict : Result::Ready;
    }
    Result advance(const RevisionReservation& current, ReservationPhase phase, RevisionReservation& verify, uint8_t* scratch, size_t capacity)
    {
        if (!inputs(current, verify, scratch, capacity) || !reservationValid(current) || current.sequence == UINT64_MAX) return Result::Invalid;
        verify = current;
        verify.sequence++;
        verify.phase = phase;
        if (!reservationAdvanceValid(current, verify)) return Result::Conflict;
        const auto checked = validateCurrent(current, verify, scratch, capacity);
        if (checked != Result::Ready) return checked;
        verify = current;
        verify.sequence++;
        verify.phase = phase;
        if (!encodeReservation(verify, scratch, capacity)) return Result::Invalid;
        const auto result = persist(current.sequence + 1, verify, scratch);
        if (result != Result::Ready) return result;
        return verify.phase == phase && reservationAdvanceValid(current, verify) ? Result::Ready : Result::StorageFailure;
    }

  private:
    bool inputs(const RevisionReservation& a, const RevisionReservation& b, uint8_t* scratch, size_t capacity) const
    {
        if (&a == &b || !scratch || capacity < kReservationBytes || !a_ || !b_ || !*a_ || !*b_) return false;
        for (size_t i = 0;; ++i)
        {
            const auto lower = [](char c)
            { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
            if (lower(a_[i]) != lower(b_[i])) return true;
            if (!a_[i]) return false;
        }
    }
    static bool binding(const RevisionReservation& a, const RevisionReservation& b) { return a.generation == b.generation && a.revision == b.revision && !std::memcmp(a.operation, b.operation, 16) && !std::memcmp(a.object, b.object, 32) && !std::memcmp(a.author, b.author, 32) && !std::memcmp(a.parent, b.parent, 32) && !std::memcmp(a.candidate, b.candidate, 32); }
    static bool same(const RevisionReservation& a, const RevisionReservation& b) { return binding(a, b) && a.sequence == b.sequence && a.phase == b.phase; }
    Result read(const char* path, RevisionReservation& output, uint8_t* scratch)
    {
        output = {};
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? Result::Missing : Result::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size);
        const bool read = sized && size == kReservationBytes && io_.seek(opened.handle, 0) && io_.read(opened.handle, scratch, kReservationBytes) == kReservationBytes;
        const bool closed = io_.close(opened.handle);
        if (!sized || !closed) return Result::StorageFailure;
        if (size != kReservationBytes) return Result::Corrupt;
        if (!read) return Result::StorageFailure;
        return decodeReservation(scratch, kReservationBytes, output) ? Result::Ready : Result::Corrupt;
    }
    Result persist(uint64_t sequence, RevisionReservation& verify, uint8_t* scratch)
    {
        const char* path = sequence & 1 ? a_ : b_;
        const auto opened = io_.open(path, Io::Mode::CreateTemporary);
        if (!opened.handle) return Result::StorageFailure;
        const bool written = io_.write(opened.handle, scratch, kReservationBytes) == kReservationBytes && io_.sync(opened.handle), closed = io_.close(opened.handle);
        if (!written || !closed) return Result::StorageFailure;
        const auto result = read(path, verify, scratch);
        return result == Result::Ready && verify.sequence == sequence ? Result::Ready : Result::StorageFailure;
    }
    Io& io_;
    const char* a_;
    const char* b_;
};
} // namespace track_sharing
