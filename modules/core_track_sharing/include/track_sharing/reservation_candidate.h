#pragma once
#include "track_sharing/manifest_hash.h"
#include "track_sharing/revision_reservation.h"
namespace track_sharing
{
// Binds immutable candidate bytes to a local reservation, not file/parent
// authenticity or authorization. Identity fields must be from one device read.
template <class Hash>
bool reservationMatchesCandidate(geocaching::ByteView raw, geocaching::ByteView device_author, uint32_t generation,
                                 geocaching::ByteView author_hash, const RevisionReservation& reservation,
                                 Hash& hash, ManifestView& view, uint8_t* scratch, size_t capacity)
{
    if (!scratch || capacity < 64 || !device_author.data || device_author.size != 64 || !author_hash.data || author_hash.size != 32 ||
        !reservationValid(reservation) || (reservation.phase != ReservationPhase::Reserved && reservation.phase != ReservationPhase::Checked) ||
        generation != reservation.generation || std::memcmp(author_hash.data, reservation.author, 32) ||
        signingSpansOverlap(raw.data, raw.size, reinterpret_cast<const uint8_t*>(&view), sizeof(view)) ||
        signingSpansOverlap(scratch, 64, reinterpret_cast<const uint8_t*>(&reservation), sizeof(reservation)) ||
        signingSpansOverlap(scratch, 64, reinterpret_cast<const uint8_t*>(&view), sizeof(view)) ||
        signingSpansOverlap(scratch, 64, device_author.data, 64) || signingSpansOverlap(scratch, 64, author_hash.data, 32)) return false;
    if (!deriveManifestHashes(raw, view, hash, scratch, scratch + 32)) return false;
    return view.revision == reservation.revision && !std::memcmp(view.author.data, device_author.data, 64) &&
           !std::memcmp(scratch, reservation.object, 32) && !std::memcmp(scratch + 32, reservation.candidate, 32) &&
           (view.revision == 1 ? !view.previous.size : view.previous.size == 32 && !std::memcmp(view.previous.data, reservation.parent, 32));
}
} // namespace track_sharing
