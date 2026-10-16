#pragma once
#include "chat/ports/device_author.h"
#include "track_sharing/reservation_candidate.h"
#include "track_sharing/reservation_store.h"
#include "track_sharing/signed_manifest_store.h"
namespace track_sharing
{
enum class PreparedSignatureResult : uint8_t
{
    Ready,
    NotChecked,
    Conflict,
    StaleIdentity,
    StorageFailure,
    Invalid,
    Unavailable
};
// Caller retains immutable candidate/file/parent lease and current-head
// reservation. No signature is returned before blob and Signed log are durable.
template <class Check, class Hash, class Adapter, class BlobStore, class JournalStore>
PreparedSignatureResult persistPreparedSignature(Check& check, geocaching::ByteView raw, geocaching::ByteView author, uint32_t generation,
                                                 geocaching::ByteView author_hash, RevisionReservation& current, RevisionReservation& verify,
                                                 Hash& hash, Adapter& adapter, BlobStore& blobs, JournalStore& journal, ManifestView& view,
                                                 uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity)
{
    using Result = PreparedSignatureResult;
    if (!raw.data || !raw.size || raw.size > 4096 || !workspace || capacity < kReservationBytes || capacity < sizeof("TrailMate.Track.Manifest.v1") + raw.size || !signature || signature_capacity < 64 || &current == &verify ||
        signingSpansOverlap(workspace, capacity, raw.data, raw.size) || signingSpansOverlap(signature, 64, raw.data, raw.size) || signingSpansOverlap(workspace, capacity, signature, 64) ||
        signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(&current), sizeof(current)) || signingSpansOverlap(workspace, capacity, reinterpret_cast<const uint8_t*>(&verify), sizeof(verify))) return Result::Invalid;
    const auto overlaps_output = [&](const uint8_t* source, size_t size)
    { return signingSpansOverlap(workspace, capacity, source, size) || signingSpansOverlap(signature, 64, source, size); };
    if (overlaps_output(author.data, author.size) || overlaps_output(author_hash.data, author_hash.size) ||
        overlaps_output(reinterpret_cast<const uint8_t*>(&current), sizeof(current)) || overlaps_output(reinterpret_cast<const uint8_t*>(&verify), sizeof(verify)) ||
        overlaps_output(reinterpret_cast<const uint8_t*>(&view), sizeof(view)) || overlaps_output(reinterpret_cast<const uint8_t*>(&check), sizeof(check)) ||
        overlaps_output(reinterpret_cast<const uint8_t*>(&hash), sizeof(hash)) || overlaps_output(reinterpret_cast<const uint8_t*>(&adapter), sizeof(adapter)) ||
        overlaps_output(reinterpret_cast<const uint8_t*>(&blobs), sizeof(blobs)) || overlaps_output(reinterpret_cast<const uint8_t*>(&journal), sizeof(journal))) return Result::Invalid;
    std::memset(signature, 0, 64);
    const auto fail = [&](Result result)
    {std::memset(signature,0,64);return result; };
    if (!check.ready()) return Result::NotChecked;
    if (current.phase != ReservationPhase::Checked || !reservationMatchesCandidate(raw, author, generation, author_hash, current, hash, view, workspace, capacity) || !check.matchesCandidate(view)) return Result::Conflict;
    const auto selected = journal.validateCurrent(current, verify, workspace, capacity);
    if (selected != ReservationStoreResult::Ready) return selected == ReservationStoreResult::Conflict ? Result::Conflict : Result::StorageFailure;
    const auto signed_result = adapter.signTrackManifest(raw.data, raw.size, generation, view, workspace, capacity, signature, signature_capacity);
    if (signed_result != chat::DeviceAuthorResult::Ok) return fail(signed_result == chat::DeviceAuthorResult::StaleIdentity ? Result::StaleIdentity : signed_result == chat::DeviceAuthorResult::StorageError ? Result::StorageFailure
                                                                                                                                                                                                              : Result::Unavailable);
    const auto stored = blobs.persist(raw, {signature, 64}, workspace, capacity);
    if (stored != SignedStoreResult::Ready) return fail(stored == SignedStoreResult::Conflict ? Result::Conflict : Result::StorageFailure);
    const auto advanced = journal.advance(current, ReservationPhase::Signed, verify, workspace, capacity);
    if (advanced != ReservationStoreResult::Ready) return fail(advanced == ReservationStoreResult::Conflict ? Result::Conflict : Result::StorageFailure);
    current = verify;
    return Result::Ready;
}
} // namespace track_sharing
