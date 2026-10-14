#pragma once
#include "track_sharing/network_upload_response.h"
#include "track_sharing/persist_upload_response.h"
#include "track_sharing/publication_journal_store.h"
#include "track_sharing/verify_manifest.h"

namespace track_sharing
{
inline bool publicationManifestMatches(const PublicationJournal& job, const VerifiedManifestView& proof)
{
    return publicationJournalValid(job) && proof.verified && proof.manifest.raw.data && proof.manifest.raw.size &&
           proof.manifest.raw.size <= 4096 && proof.signature.data && proof.signature.size == 64 &&
           proof.manifest.file_hash.data && proof.manifest.file_hash.size == 32 &&
           proof.manifest.file_bytes == job.file_bytes && proof.manifest.revision == job.revision &&
           !std::memcmp(proof.object_id, job.object, 32) && !std::memcmp(proof.revision_hash, job.revision_hash, 32) &&
           !std::memcmp(proof.manifest.file_hash.data, job.file_hash, 32);
}

// The durable operation ID is also the network PUBLISH request ID. Recovery
// resends exactly the same signed version and obtains the cached receipt.
inline bool encodeTrackPublishRequest(const PublicationJournal& job, const VerifiedManifestView& proof,
                                      uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!publicationManifestMatches(job, proof) || job.phase != PublicationPhase::Publishing ||
        !output || !capacity || (proof.manifest.previous.size && (!proof.manifest.previous.data || proof.manifest.previous.size != 32))) return false;
    for (const auto input : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&job), sizeof(job)},
                             {reinterpret_cast<const uint8_t*>(&proof), sizeof(proof)},
                             proof.manifest.raw,
                             proof.signature,
                             proof.manifest.previous})
        if (input.size && !networkInputDisjoint(input, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!networkRequestHeader(writer, 7, {job.operation, 16}, 8192, 2) || !writer.array(2) ||
        !writer.binary(proof.manifest.raw) || !writer.binary(proof.signature) ||
        !(proof.manifest.previous.size ? writer.binary(proof.manifest.previous) : writer.nil())) return false;
    written = writer.size();
    return writer.good();
}

struct PublishResponseView
{
    geocaching::ByteView signed_receipt;
    bool current = false;
};
// Structural response binding only. Caller must authenticate the transport and
// verify the receipt's directory key, epoch, version and Ed25519 signature.
inline bool decodeTrackPublishResponse(geocaching::ByteView bytes, geocaching::ByteView request, PublishResponseView& output)
{
    if (!bytes.data || !bytes.size || bytes.size > 8192 ||
        !networkInputDisjoint(bytes, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader(bytes);
    if (!uploadResponseHeader(reader, 7, request, 2)) return false;
    const auto start = reader.position();
    size_t count = 0;
    geocaching::ByteView raw, signature;
    if (!reader.array(count, 2) || count != 2 || !reader.binary(raw, 1024) || !raw.size ||
        !reader.binary(signature, 64) || signature.size != 64) return false;
    const auto end = reader.position();
    uint64_t current = 0;
    if (!reader.unsignedInteger(current) || current > 1 || !reader.finished()) return false;
    output = {{bytes.data + start, end - start}, current != 0};
    return true;
}

template <class Store>
PublicationStoreResult markPublicationSending(Store& store, PublicationJournal& current, PublicationJournal& next,
                                              PublicationJournal& verify, const VerifiedManifestView& manifest,
                                              uint8_t* scratch, size_t capacity)
{
    if (!publicationManifestMatches(current, manifest) || &current == &next || &current == &verify || &next == &verify ||
        !uploadJournalWorkspace(current, next, verify, manifest.manifest.raw, manifest.signature, {}, scratch, capacity) ||
        signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(&manifest), sizeof(manifest))) return PublicationStoreResult::Invalid;
    if (current.phase == PublicationPhase::Publishing) return store.validateCurrent(current, verify, scratch, capacity);
    if (current.phase != PublicationPhase::FileStored || current.sequence == UINT64_MAX) return PublicationStoreResult::Invalid;
    next = current;
    next.phase = PublicationPhase::Publishing;
    ++next.sequence;
    const auto result = store.advance(current, next, verify, scratch, capacity);
    if (result == PublicationStoreResult::Ready) current = next;
    return result;
}
} // namespace track_sharing
