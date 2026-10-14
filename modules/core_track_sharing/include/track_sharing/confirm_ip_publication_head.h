#pragma once
#include "track_sharing/persist_signed_head.h"
#include "track_sharing/signed_manifest_store.h"
#include "track_sharing/verify_receipt.h"

namespace track_sharing
{
// Computer IP publication has no device RF task. A verified, durably stored
// directory receipt supplies the evidence needed to permit the next revision.
// Caller owns immutable verified views, current identity/media and exclusive
// object/receipt paths. The expected journal is binding metadata, not an RF job.
// Receipt persistence precedes head advancement so an interrupted advance can
// be retried without losing evidence or signing/publishing again.
template <class Heads, class Receipts>
HeadStoreResult confirmIpPublicationHead(Heads& heads, Receipts& receipts,
                                         const VerifiedReceiptView& proof, const PublicationJournal& expected,
                                         geocaching::ByteView request, geocaching::ByteView directory_public,
                                         const VerifiedManifestView& manifest, const RevisionReservation& reservation,
                                         uint32_t generation, geocaching::ByteView author, geocaching::ByteView author_hash,
                                         ObjectHead& current, ObjectHead& next, ObjectHead& verify,
                                         uint8_t* scratch, size_t capacity)
{
    if (&current == &next || &current == &verify || &next == &verify ||
        !proof.verified || !proof.signature.data || proof.signature.size != 64 ||
        !proof.receipt.raw.data || !proof.receipt.raw.size || !manifest.verified ||
        !manifest.manifest.file_hash.data || manifest.manifest.file_hash.size != 32 ||
        !author_hash.data || author_hash.size != 32 || expected.generation != generation ||
        (expected.phase != PublicationPhase::Publishing && expected.phase != PublicationPhase::Confirmed) ||
        !receiptMatchesJob(proof.receipt, expected, request, directory_public, manifest.manifest.state) ||
        expected.revision != manifest.manifest.revision || expected.file_bytes != manifest.manifest.file_bytes ||
        std::memcmp(expected.object, manifest.object_id, 32) ||
        std::memcmp(expected.revision_hash, manifest.revision_hash, 32) ||
        std::memcmp(expected.file_hash, manifest.manifest.file_hash.data, 32) ||
        std::memcmp(expected.author_hash, author_hash.data, 32)) return HeadStoreResult::Invalid;
    if (!scratch || capacity < 128) return HeadStoreResult::Invalid;
    for (const auto input : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&expected), sizeof(expected)},
                             proof.receipt.raw, proof.signature, author, author_hash,
                             manifest.manifest.raw, manifest.signature})
        if (signingSpansOverlap(input.data, input.size, scratch, capacity)) return HeadStoreResult::Invalid;

    const auto loaded = heads.load(current, verify, scratch, capacity);
    if (loaded != HeadStoreResult::Ready) return loaded;
    if (std::memcmp(current.operation, expected.operation, 16) ||
        !signedHeadMatches(current, manifest, reservation, generation, author, author_hash)) return HeadStoreResult::Conflict;
    if (current.phase != HeadPhase::Committed && current.sequence == UINT64_MAX) return HeadStoreResult::Conflict;
    const auto persisted = receipts.persist(proof.receipt.raw, proof.signature, scratch, capacity);
    if (persisted != SignedStoreResult::Ready)
        return persisted == SignedStoreResult::StorageFailure ? HeadStoreResult::StorageFailure : HeadStoreResult::Conflict;
    // Even a Committed head needs the exact receipt to be durably present.
    if (current.phase == HeadPhase::Committed) return HeadStoreResult::Ready;
    next = current;
    ++next.sequence;
    next.phase = HeadPhase::Committed;
    const auto advanced = heads.advance(current, next, verify, scratch, capacity);
    if (advanced == HeadStoreResult::Ready) current = next;
    return advanced;
}
} // namespace track_sharing
