#pragma once
#include "track_sharing/object_head_store.h"
#include "track_sharing/signed_object_head.h"
namespace track_sharing
{
// Caller holds immutable signed bytes, current identity and object exclusive
// lease. All records/evidence/paths/scratch are disjoint admitted storage.
inline bool signedHeadMatches(const ObjectHead& head, const VerifiedManifestView& signed_view,
                              const RevisionReservation& reservation, uint32_t generation,
                              geocaching::ByteView author, geocaching::ByteView author_hash)
{
    if (!objectHeadValid(head) || !signed_view.verified || !reservationValid(reservation) || reservation.phase != ReservationPhase::Signed ||
        !author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 || !generation || head.generation != generation || reservation.generation != generation ||
        !signed_view.manifest.author.data || signed_view.manifest.author.size != 64 || signed_view.manifest.revision != head.revision || reservation.revision != head.revision ||
        signed_view.manifest.state > 1 || head.archived != (signed_view.manifest.state == 1)) return false;
    if (std::memcmp(author.data, signed_view.manifest.author.data, 64) || std::memcmp(author_hash.data, reservation.author, 32) ||
        std::memcmp(head.object, signed_view.object_id, 32) || std::memcmp(head.object, reservation.object, 32) ||
        std::memcmp(head.hash, signed_view.revision_hash, 32) || std::memcmp(head.hash, reservation.candidate, 32) ||
        std::memcmp(head.operation, reservation.operation, 16) || std::memcmp(head.parent, reservation.parent, 32)) return false;
    return head.revision == 1 ? signed_view.manifest.previous.size == 0 : signed_view.manifest.previous.data && signed_view.manifest.previous.size == 32 && !std::memcmp(head.parent, signed_view.manifest.previous.data, 32);
}
template <class Store>
HeadStoreResult persistSignedObjectHead(Store& store, const VerifiedManifestView& signed_view, const RevisionReservation& reservation,
                                        uint32_t generation, geocaching::ByteView author, geocaching::ByteView author_hash,
                                        ObjectHead& current, ObjectHead& next, ObjectHead& verify, uint8_t* scratch, size_t capacity)
{
    if (&current == &next || &current == &verify || &next == &verify) return HeadStoreResult::Invalid;
    const auto loaded = store.load(current, verify, scratch, capacity);
    if (loaded != HeadStoreResult::Ready && loaded != HeadStoreResult::Missing) return loaded;
    if (loaded == HeadStoreResult::Ready && signedHeadMatches(current, signed_view, reservation, generation, author, author_hash)) return HeadStoreResult::Ready;
    if (!createSignedObjectHead(signed_view, reservation, generation, author, author_hash, loaded == HeadStoreResult::Missing ? nullptr : &current, next)) return HeadStoreResult::Conflict;
    const auto result = loaded == HeadStoreResult::Missing ? store.initialize(next, verify, scratch, capacity) : store.advance(current, next, verify, scratch, capacity);
    if (result == HeadStoreResult::Ready) current = next;
    return result;
}
} // namespace track_sharing
