#pragma once
#include "track_sharing/create_reservation.h"
#include "track_sharing/object_head.h"
#include "track_sharing/prepare_request.h"
#include "track_sharing/reservation_store.h"
namespace track_sharing
{
// Caller holds immutable inputs and exclusive current-object lease. Both null
// parent/head require independently verified absence, including orphan state.
template <class Check, class Hash, class Store>
ReservationStoreResult persistCheckedReservation(Check& check, const PrepareRequest& request, geocaching::ByteView raw,
                                                 geocaching::ByteView author, geocaching::ByteView author_hash, uint32_t generation,
                                                 const ObjectHead* head, const VerifiedManifestView* parent, Hash& hash, Store& store,
                                                 ManifestView& view, RevisionReservation& desired, RevisionReservation& current,
                                                 RevisionReservation& verify, uint8_t* scratch, size_t capacity)
{
    if (!check.ready() || request.generation != generation || &desired == &current || &desired == &verify || &current == &verify || !scratch || capacity < kReservationBytes) return ReservationStoreResult::Invalid;
    if ((head == nullptr) != (parent == nullptr)) return ReservationStoreResult::Conflict;
    if (head)
    {
        if (!objectHeadValid(*head) || head->phase != HeadPhase::Committed || head->generation != generation || !parent->verified ||
            parent->manifest.revision != head->revision || parent->manifest.state != (head->archived ? 1 : 0) ||
            std::memcmp(parent->object_id, head->object, 32) || std::memcmp(parent->revision_hash, head->hash, 32) ||
            !request.expected_parent.data || request.expected_parent.size != 32 || std::memcmp(request.expected_parent.data, head->hash, 32)) return ReservationStoreResult::Conflict;
    }
    else if (request.expected_parent.size) return ReservationStoreResult::Conflict;
    if (!createCandidateReservation(raw, author, generation, author_hash, request.operation, parent, hash, view, desired) || !check.matchesCandidate(view)) return ReservationStoreResult::Conflict;
    auto result = store.load(current, verify, scratch, capacity);
    if (result == ReservationStoreResult::Missing)
    {
        result = store.initialize(desired, verify, scratch, capacity);
        if (result != ReservationStoreResult::Ready) return result;
        current = desired;
    }
    else if (result != ReservationStoreResult::Ready) return result;
    if (current.generation != desired.generation || current.revision != desired.revision || std::memcmp(current.operation, desired.operation, 16) ||
        std::memcmp(current.object, desired.object, 32) || std::memcmp(current.author, desired.author, 32) ||
        std::memcmp(current.parent, desired.parent, 32) || std::memcmp(current.candidate, desired.candidate, 32)) return ReservationStoreResult::Conflict;
    if (current.phase == ReservationPhase::Checked) return store.validateCurrent(current, verify, scratch, capacity);
    if (current.phase != ReservationPhase::Reserved) return ReservationStoreResult::Conflict;
    result = store.advance(current, ReservationPhase::Checked, verify, scratch, capacity);
    if (result == ReservationStoreResult::Ready) current = verify;
    return result;
}
} // namespace track_sharing
