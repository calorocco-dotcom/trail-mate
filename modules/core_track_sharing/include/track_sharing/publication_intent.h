#pragma once
#include "track_sharing/commit_request.h"
#include "track_sharing/persist_signed_head.h"
#include "track_sharing/publication_journal.h"
namespace track_sharing
{
// Constructs bindings only after signed-head and independent blob verification.
// Caller separately validates directory protocol/epoch, media and task quota.
template <class FileCheck>
bool createPublicationIntent(const CommitRequest& request, const ObjectHead& head,
                             const VerifiedManifestView& signed_view, const RevisionReservation& reservation,
                             geocaching::ByteView author, geocaching::ByteView author_hash, const FileCheck& file,
                             PublicationJournal& output)
{
    const auto* target = reinterpret_cast<const uint8_t*>(&output);
    for (const auto field : {request.operation, request.object, request.revision_hash, request.directory, request.directory_epoch, author, author_hash,
                             geocaching::ByteView{reinterpret_cast<const uint8_t*>(&head), sizeof(head)},
                             geocaching::ByteView{reinterpret_cast<const uint8_t*>(&signed_view), sizeof(signed_view)},
                             geocaching::ByteView{reinterpret_cast<const uint8_t*>(&reservation), sizeof(reservation)},
                             geocaching::ByteView{reinterpret_cast<const uint8_t*>(&file), sizeof(file)}, signed_view.manifest.raw, signed_view.signature})
        if (signingSpansOverlap(target, sizeof(output), field.data, field.size)) return false;
    output = {};
    if (head.phase != HeadPhase::Signed || !request.operation.data || request.operation.size != 16 || !request.object.data || request.object.size != 32 ||
        !request.revision_hash.data || request.revision_hash.size != 32 || !request.directory.data || request.directory.size != 16 ||
        !request.directory_epoch.data || request.directory_epoch.size != 16 || request.revision != head.revision ||
        request.generation != head.generation || std::memcmp(request.operation.data, head.operation, 16) ||
        std::memcmp(request.object.data, head.object, 32) || std::memcmp(request.revision_hash.data, head.hash, 32) ||
        !signedHeadMatches(head, signed_view, reservation, request.generation, author, author_hash) ||
        !file.matchesExpected(signed_view.manifest.file_bytes, signed_view.manifest.file_hash.data)) return false;
    std::memcpy(output.operation, head.operation, 16);
    std::memcpy(output.object, head.object, 32);
    std::memcpy(output.revision_hash, head.hash, 32);
    std::memcpy(output.directory, request.directory.data, 16);
    std::memcpy(output.directory_epoch, request.directory_epoch.data, 16);
    std::memcpy(output.file_hash, signed_view.manifest.file_hash.data, 32);
    std::memcpy(output.author_hash, author_hash.data, 32);
    output.generation = head.generation;
    output.revision = head.revision;
    output.file_bytes = signed_view.manifest.file_bytes;
    output.sequence = 1;
    return publicationJournalValid(output);
}
} // namespace track_sharing
