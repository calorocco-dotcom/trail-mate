#pragma once
#include "track_sharing/object_head.h"
#include "track_sharing/revision_reservation.h"
#include "track_sharing/verify_manifest.h"
namespace track_sharing
{
// Caller holds current-head exclusive lease; null current requires verified
// absence. Verified view must come from immutable stored signature bytes.
// This creates Signed only; Committed requires separate durable task evidence.
inline bool createSignedObjectHead(const VerifiedManifestView& signed_view, const RevisionReservation& reservation,
                                   uint32_t generation, geocaching::ByteView author, geocaching::ByteView author_hash,
                                   const ObjectHead* current, ObjectHead& output)
{
    const auto overlaps = [&](const void* input, size_t size)
    { const auto a = reinterpret_cast<uintptr_t>(input), b = reinterpret_cast<uintptr_t>(&output); return input && (a <= b ? b - a < size : a - b < sizeof(output)); };
    if (overlaps(&signed_view, sizeof(signed_view)) || overlaps(&reservation, sizeof(reservation)) ||
        overlaps(signed_view.manifest.raw.data, signed_view.manifest.raw.size) || overlaps(signed_view.signature.data, signed_view.signature.size) ||
        overlaps(author.data, author.size) || overlaps(author_hash.data, author_hash.size) || (current && overlaps(current, sizeof(*current)))) return false;
    output = {};
    if (!signed_view.verified || !reservationValid(reservation) || reservation.phase != ReservationPhase::Signed || !generation || reservation.generation != generation ||
        !author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 || signed_view.manifest.author.size != 64 || !signed_view.manifest.author.data ||
        std::memcmp(signed_view.manifest.author.data, author.data, 64) || std::memcmp(reservation.author, author_hash.data, 32) ||
        std::memcmp(signed_view.object_id, reservation.object, 32) || std::memcmp(signed_view.revision_hash, reservation.candidate, 32) ||
        signed_view.manifest.revision != reservation.revision || signed_view.manifest.state > 1) return false;
    if (reservation.revision != 1 && (!signed_view.manifest.previous.data || signed_view.manifest.previous.size != 32 || std::memcmp(reservation.parent, signed_view.manifest.previous.data, 32))) return false;
    if (current && (!objectHeadValid(*current) || current->sequence == UINT64_MAX || current->revision == UINT32_MAX || reservation.revision != current->revision + 1)) return false;
    std::memcpy(output.object, reservation.object, 32); std::memcpy(output.hash, reservation.candidate, 32);
    std::memcpy(output.parent, reservation.parent, 32); std::memcpy(output.operation, reservation.operation, 16);
    output.generation = generation; output.revision = reservation.revision; output.sequence = current ? current->sequence + 1 : 1;
    output.archived = signed_view.manifest.state == 1;
    if (!objectHeadValid(output) || (current ? !objectHeadAdvanceValid(*current, output) : output.revision != 1)) { output = {}; return false; }
    return true;
}
} // namespace track_sharing
