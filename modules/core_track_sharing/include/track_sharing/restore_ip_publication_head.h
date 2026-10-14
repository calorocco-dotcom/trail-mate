#pragma once
#include "track_sharing/confirm_ip_publication_head.h"
namespace track_sharing
{
// Recover the same author's confirmed asset on a replacement device. Immutable
// signed manifest and directory receipt must both persist BEFORE head creation.
// Only the exact verified signed head may be completed; different heads and
// corrupt journals never authorize recovery.
template <class Heads, class Blobs, class Receipts>
HeadStoreResult restoreIpPublicationHead(Heads& heads, Blobs& blobs, Receipts& receipts,
                                         const VerifiedManifestView& manifest, const VerifiedReceiptView& proof,
                                         const PublicationJournal& expected, geocaching::ByteView request,
                                         geocaching::ByteView directory_public, uint32_t generation,
                                         geocaching::ByteView author, geocaching::ByteView author_hash,
                                         ObjectHead& current, ObjectHead& next, ObjectHead& verify,
                                         uint8_t* scratch, size_t capacity)
{
    if (!scratch || capacity < kObjectHeadBytes) return HeadStoreResult::Invalid;
    for (const auto input : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&manifest), sizeof(manifest)},
                             {reinterpret_cast<const uint8_t*>(&proof), sizeof(proof)},
                             {reinterpret_cast<const uint8_t*>(&expected), sizeof(expected)},
                             manifest.manifest.raw,
                             manifest.manifest.author,
                             manifest.manifest.previous,
                             manifest.manifest.file_hash,
                             manifest.signature,
                             proof.receipt.raw,
                             proof.signature,
                             request,
                             directory_public,
                             author,
                             author_hash})
        for (const auto output : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&current), sizeof(current)},
                                  {reinterpret_cast<const uint8_t*>(&next), sizeof(next)},
                                  {reinterpret_cast<const uint8_t*>(&verify), sizeof(verify)},
                                  {scratch, capacity}})
            if (signingSpansOverlap(input.data, input.size, output.data, output.size)) return HeadStoreResult::Invalid;
    if (&current == &next || &current == &verify || &next == &verify || !generation || expected.generation != generation ||
        !manifest.verified || !proof.verified || !author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 ||
        !manifest.manifest.author.data || manifest.manifest.author.size != 64 || std::memcmp(author.data, manifest.manifest.author.data, 64) ||
        !manifest.manifest.raw.data || !manifest.manifest.raw.size || !manifest.signature.data || manifest.signature.size != 64 ||
        !proof.receipt.raw.data || !proof.receipt.raw.size || !proof.signature.data || proof.signature.size != 64 ||
        !manifest.manifest.file_hash.data || manifest.manifest.file_hash.size != 32 ||
        !receiptMatchesJob(proof.receipt, expected, request, directory_public, manifest.manifest.state) ||
        expected.revision != manifest.manifest.revision || expected.file_bytes != manifest.manifest.file_bytes ||
        std::memcmp(expected.author_hash, author_hash.data, 32) || std::memcmp(expected.object, manifest.object_id, 32) ||
        std::memcmp(expected.revision_hash, manifest.revision_hash, 32) || std::memcmp(expected.file_hash, manifest.manifest.file_hash.data, 32))
        return HeadStoreResult::Invalid;
    next = {};
    std::memcpy(next.object, manifest.object_id, 32);
    std::memcpy(next.hash, manifest.revision_hash, 32);
    std::memcpy(next.operation, expected.operation, 16);
    if (manifest.manifest.revision > 1)
    {
        if (!manifest.manifest.previous.data || manifest.manifest.previous.size != 32) return HeadStoreResult::Invalid;
        std::memcpy(next.parent, manifest.manifest.previous.data, 32);
    }
    next.sequence = 1;
    next.generation = generation;
    next.revision = manifest.manifest.revision;
    next.phase = HeadPhase::Committed;
    next.archived = manifest.manifest.state == 1;
    if (!objectHeadValid(next)) return HeadStoreResult::Invalid;
    const auto loaded = heads.load(current, verify, scratch, capacity);
    if (loaded != HeadStoreResult::Ready && loaded != HeadStoreResult::Missing) return loaded;
    if (loaded == HeadStoreResult::Ready && (current.revision != next.revision ||
                                             current.archived != next.archived || std::memcmp(current.object, next.object, 32) || std::memcmp(current.hash, next.hash, 32) ||
                                             std::memcmp(current.parent, next.parent, 32))) return HeadStoreResult::Conflict;
    const auto stored = blobs.persist(manifest.manifest.raw, manifest.signature, scratch, capacity);
    if (stored != SignedStoreResult::Ready) return stored == SignedStoreResult::StorageFailure ? HeadStoreResult::StorageFailure : HeadStoreResult::Conflict;
    const auto receipt = receipts.persist(proof.receipt.raw, proof.signature, scratch, capacity);
    if (receipt != SignedStoreResult::Ready) return receipt == SignedStoreResult::StorageFailure ? HeadStoreResult::StorageFailure : HeadStoreResult::Conflict;
    if (loaded == HeadStoreResult::Ready)
    {
        if (current.phase == HeadPhase::Signed)
        {
            // Preserve the original signing operation and generation. Persist
            // the ordinary commit first; a retry after interruption can then
            // adopt that verified committed head at the current generation.
            if (current.sequence == UINT64_MAX) return HeadStoreResult::Invalid;
            next = current;
            ++next.sequence;
            next.phase = HeadPhase::Committed;
            const auto committed = heads.advance(current, next, verify, scratch, capacity);
            if (committed != HeadStoreResult::Ready) return committed;
            current = next;
        }
        return heads.rebindCommittedGeneration(current, verify, generation, scratch, capacity);
    }
    const auto initialized = heads.initializeCommitted(next, verify, scratch, capacity);
    if (initialized == HeadStoreResult::Ready) current = next;
    return initialized;
}
} // namespace track_sharing
