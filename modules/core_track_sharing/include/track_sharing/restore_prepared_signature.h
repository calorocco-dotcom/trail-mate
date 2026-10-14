#pragma once
#include "track_sharing/prepared_signature.h"
#include "track_sharing/verify_manifest.h"
namespace track_sharing
{
// Caller holds immutable verified blob/file/current-parent lease. No signer
// participates: recovery validates durable evidence before returning signature.
template <class Check, class JournalStore>
PreparedSignatureResult restorePreparedSignature(Check& check, const VerifiedManifestView& signed_view,
                                                 geocaching::ByteView author, uint32_t generation, geocaching::ByteView author_hash,
                                                 RevisionReservation& current, RevisionReservation& verify, JournalStore& journal,
                                                 uint8_t* scratch, size_t capacity, uint8_t* signature, size_t signature_capacity)
{
    using Result = PreparedSignatureResult;
    if (!scratch || capacity < kReservationBytes || !signature || signature_capacity < 64 || !author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 || !generation || &current == &verify || signingSpansOverlap(scratch, capacity, signature, 64)) return Result::Invalid;
    const auto overlaps = [&](const uint8_t* source, size_t size)
    { return signingSpansOverlap(scratch, capacity, source, size) || signingSpansOverlap(signature, 64, source, size); };
    if (overlaps(signed_view.manifest.raw.data, signed_view.manifest.raw.size) || overlaps(signed_view.signature.data, signed_view.signature.size) ||
        overlaps(author.data, author.size) || overlaps(author_hash.data, author_hash.size) || overlaps(reinterpret_cast<const uint8_t*>(&signed_view), sizeof(signed_view)) ||
        overlaps(reinterpret_cast<const uint8_t*>(&current), sizeof(current)) || overlaps(reinterpret_cast<const uint8_t*>(&verify), sizeof(verify)) ||
        overlaps(reinterpret_cast<const uint8_t*>(&check), sizeof(check)) || overlaps(reinterpret_cast<const uint8_t*>(&journal), sizeof(journal))) return Result::Invalid;
    std::memset(signature, 0, 64);
    if (!check.ready()) return Result::NotChecked;
    if (!reservationValid(current) || (current.phase != ReservationPhase::Checked && current.phase != ReservationPhase::Signed) || !signed_view.verified || !signed_view.signature.data || signed_view.signature.size != 64) return Result::Conflict;
    if (current.generation != generation || std::memcmp(current.author, author_hash.data, 32)) return Result::StaleIdentity;
    const auto& manifest = signed_view.manifest;
    if (!manifest.author.data || manifest.author.size != 64 || std::memcmp(manifest.author.data, author.data, 64) || manifest.revision != current.revision ||
        std::memcmp(signed_view.object_id, current.object, 32) || std::memcmp(signed_view.revision_hash, current.candidate, 32) || !check.matchesCandidate(manifest) ||
        (manifest.revision > 1 && (!manifest.previous.data || manifest.previous.size != 32 || std::memcmp(manifest.previous.data, current.parent, 32)))) return Result::Conflict;
    const auto selected = journal.validateCurrent(current, verify, scratch, capacity);
    if (selected != ReservationStoreResult::Ready) return selected == ReservationStoreResult::Conflict ? Result::Conflict : Result::StorageFailure;
    if (current.phase == ReservationPhase::Checked)
    {
        const auto advanced = journal.advance(current, ReservationPhase::Signed, verify, scratch, capacity);
        if (advanced != ReservationStoreResult::Ready) return advanced == ReservationStoreResult::Conflict ? Result::Conflict : Result::StorageFailure;
        current = verify;
    }
    std::memcpy(signature, signed_view.signature.data, 64);
    return Result::Ready;
}
} // namespace track_sharing
