#pragma once
#include "platform/esp/idf_common/hostlink_track_publication_admission.h"
namespace hostlink::management::publication_runtime
{
// Candidate metadata is untrusted. This only starts sliced authentication;
// Busy plus non-null output means work admitted, never authenticated success.
inline Status beginHistoryAuthentication(chat::IMeshAdapter& adapter,
                                         const track_sharing::PublicationJournal& candidate, const track_sharing::PublicationDirectory& directory,
                                         uint32_t media, uint8_t* scratch, size_t capacity, TrackPublicationAdmission*& output)
{
    using namespace track_sharing;
    if (output) return Status::Conflict;
    if (!scratch || capacity < 448 || !publicationDirectoryMatches(directory, candidate) ||
        signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(&candidate), sizeof(candidate)) ||
        signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(&directory), sizeof(directory))) return Status::InvalidField;
    if (media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    uint8_t hash[32];
    uint32_t generation = 0;
    auto* author = scratch + capacity - 64;
    const auto identity = upload_runtime::readUploadAuthor(adapter, author, 64, hash, generation);
    if (identity != Status::Ok) return identity;
    if (generation != candidate.generation || std::memcmp(hash, candidate.author_hash, 32)) return Status::StaleIdentity;
    // Small borrowed views, immediately copied by the admission factory.
    const CommitRequest request = {{candidate.operation, 16}, {candidate.object, 32}, {candidate.revision_hash, 32}, {candidate.directory, 16}, {candidate.directory_epoch, 16}, candidate.generation, candidate.revision};
    output = createPublicationAdmission(request, directory, media, author, hash, true);
    return Status::Busy;
}
} // namespace hostlink::management::publication_runtime
