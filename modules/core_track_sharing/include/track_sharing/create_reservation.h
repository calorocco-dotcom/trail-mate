#pragma once
#include "track_sharing/candidate_revision.h"
#include "track_sharing/revision_reservation.h"
namespace track_sharing
{
// Parent is the verified head selected under platform arbitration; nullptr
// requires verified object absence. Creation alone does not persist/reserve it.
template <class Hash>
bool createCandidateReservation(geocaching::ByteView raw, geocaching::ByteView author, uint32_t generation,
                                geocaching::ByteView author_hash, geocaching::ByteView operation, const VerifiedManifestView* parent,
                                Hash& hash, ManifestView& view, RevisionReservation& output)
{
    if (!author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 || !operation.data || operation.size != 16 || !generation) return false;
    const auto overlaps = [&](const uint8_t* source, size_t size)
    { return signingSpansOverlap(reinterpret_cast<const uint8_t*>(&output), sizeof(output), source, size); };
    if (overlaps(raw.data, raw.size) || overlaps(author.data, author.size) || overlaps(author_hash.data, author_hash.size) || overlaps(operation.data, operation.size) ||
        overlaps(reinterpret_cast<const uint8_t*>(&view), sizeof(view)) || overlaps(reinterpret_cast<const uint8_t*>(&hash), sizeof(hash)) ||
        (parent && (overlaps(reinterpret_cast<const uint8_t*>(parent), sizeof(*parent)) || overlaps(parent->manifest.raw.data, parent->manifest.raw.size) || overlaps(parent->signature.data, parent->signature.size)))) return false;
    output = {};
    if (!validateCandidateRevision(raw, author, parent, view)) return false;
    output.generation = generation;
    output.revision = view.revision;
    output.sequence = 1;
    std::memcpy(output.operation, operation.data, 16);
    std::memcpy(output.author, author_hash.data, 32);
    if (view.previous.size) std::memcpy(output.parent, view.previous.data, 32);
    if (!deriveManifestHashes(raw, view, hash, output.object, output.candidate) || (parent && std::memcmp(output.object, parent->object_id, 32)) || !reservationValid(output))
    {
        output = {};
        return false;
    }
    return true;
}
} // namespace track_sharing
