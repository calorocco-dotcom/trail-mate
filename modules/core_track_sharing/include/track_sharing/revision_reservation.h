#pragma once
#include "track_sharing/upload_journal.h"
namespace track_sharing
{
enum class ReservationPhase : uint8_t
{
    Reserved,
    Checked,
    Signed,
    Committed,
    Cancelled
};
struct RevisionReservation
{
    uint8_t operation[16]{}, object[32]{}, author[32]{}, parent[32]{}, candidate[32]{};
    uint64_t sequence = 0;
    uint32_t generation = 0, revision = 0;
    ReservationPhase phase = ReservationPhase::Reserved;
};
static_assert(sizeof(RevisionReservation) <= 176, "Reservation must not own manifest/file bytes");
constexpr size_t kReservationBytes = 168;
inline bool reservationValid(const RevisionReservation& value)
{
    uint8_t operation = 0, object = 0, author = 0, parent = 0, candidate = 0;
    for (auto byte : value.operation) operation |= byte;
    for (unsigned i = 0; i < 32; ++i)
    {
        object |= value.object[i];
        author |= value.author[i];
        parent |= value.parent[i];
        candidate |= value.candidate[i];
    }
    return operation && object && author && candidate && value.sequence && value.generation && value.revision &&
           (value.revision == 1 ? !parent : parent != 0) && static_cast<uint8_t>(value.phase) <= 4;
}
// TMRV/v1/op16/object32/author-hash32/parent-revision-hash32/candidate-hash32/
// Phase progression only. Durable evidence for Checked/Signed/Committed is
// established by the platform before persistence, never by this predicate.
inline bool reservationAdvanceValid(const RevisionReservation& current, const RevisionReservation& next)
{
    if (!reservationValid(current) || !reservationValid(next) || current.sequence == UINT64_MAX || next.sequence != current.sequence + 1 ||
        current.generation != next.generation || current.revision != next.revision ||
        std::memcmp(current.operation, next.operation, 16) || std::memcmp(current.object, next.object, 32) ||
        std::memcmp(current.author, next.author, 32) || std::memcmp(current.parent, next.parent, 32) || std::memcmp(current.candidate, next.candidate, 32)) return false;
    if (current.phase == next.phase) return true;
    if (current.phase == ReservationPhase::Reserved) return next.phase == ReservationPhase::Checked || next.phase == ReservationPhase::Cancelled;
    if (current.phase == ReservationPhase::Checked) return next.phase == ReservationPhase::Signed || next.phase == ReservationPhase::Cancelled;
    if (current.phase == ReservationPhase::Signed) return next.phase == ReservationPhase::Committed;
    return false;
}
// TMRV/v1/op16/object32/author-hash32/parent-revision-hash32/candidate-hash32/
// generationLE32/revisionLE32/phase8/sequenceLE64/CRC16. Caller supplies storage.
// CRC is not author proof, reservation arbitration, or phase completion proof.
inline bool encodeReservation(const RevisionReservation& value, uint8_t* output, size_t capacity)
{
    if (!output || capacity < kReservationBytes || !reservationValid(value)) return false;
    const auto a = reinterpret_cast<uintptr_t>(&value), b = reinterpret_cast<uintptr_t>(output);
    if (a <= b ? b - a < sizeof(value) : a - b < kReservationBytes) return false;
    std::memcpy(output, "TMRV", 4);
    output[4] = 1;
    std::memcpy(output + 5, value.operation, 16);
    std::memcpy(output + 21, value.object, 32);
    std::memcpy(output + 53, value.author, 32);
    std::memcpy(output + 85, value.parent, 32);
    std::memcpy(output + 117, value.candidate, 32);
    for (unsigned i = 0; i < 4; ++i)
    {
        output[149 + i] = uint8_t(value.generation >> (8 * i));
        output[153 + i] = uint8_t(value.revision >> (8 * i));
    }
    output[157] = static_cast<uint8_t>(value.phase);
    for (unsigned i = 0; i < 8; ++i) output[158 + i] = uint8_t(value.sequence >> (8 * i));
    const auto crc = uploadJournalCrc(output, 166);
    output[166] = uint8_t(crc);
    output[167] = uint8_t(crc >> 8);
    return true;
}
inline bool decodeReservation(const uint8_t* input, size_t size, RevisionReservation& value)
{
    if (!input || size != kReservationBytes)
    {
        value = {};
        return false;
    }
    const auto a = reinterpret_cast<uintptr_t>(input), b = reinterpret_cast<uintptr_t>(&value);
    if (a <= b ? b - a < size : a - b < sizeof(value)) return false;
    value = {};
    if (std::memcmp(input, "TMRV", 4) || input[4] != 1 || uploadJournalCrc(input, 166) != (uint16_t(input[166]) | (uint16_t(input[167]) << 8))) return false;
    std::memcpy(value.operation, input + 5, 16);
    std::memcpy(value.object, input + 21, 32);
    std::memcpy(value.author, input + 53, 32);
    std::memcpy(value.parent, input + 85, 32);
    std::memcpy(value.candidate, input + 117, 32);
    for (unsigned i = 0; i < 4; ++i)
    {
        value.generation |= uint32_t(input[149 + i]) << (8 * i);
        value.revision |= uint32_t(input[153 + i]) << (8 * i);
    }
    value.phase = static_cast<ReservationPhase>(input[157]);
    for (unsigned i = 0; i < 8; ++i) value.sequence |= uint64_t(input[158 + i]) << (8 * i);
    if (!reservationValid(value))
    {
        value = {};
        return false;
    }
    return true;
}
} // namespace track_sharing
