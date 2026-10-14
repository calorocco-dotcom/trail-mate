#pragma once
#include "hostlink/route_install_request.h"
#include "track_sharing/upload_journal.h"
#include "track_sharing/upload_paths.h"
namespace hostlink::management
{
// Platform supplies session_matches from its current upload lease and
// media_ready from the same SD media session. Neither fact comes from USB.
// CRC-valid metadata alone is never sufficient authorization or file proof.
inline Status authorizeRouteInstallUpload(const RouteInstallView& request,
                                          const track_sharing::UploadJournal& journal,
                                          track_sharing::UploadKind kind,
                                          uint32_t generation,
                                          geocaching::ByteView author_hash,
                                          bool session_matches,
                                          bool media_ready)
{
    if (!request.transfer.data || request.transfer.size != 16 || !request.hash.data || request.hash.size != 32 ||
        !request.bytes || request.bytes > 8388608 || !request.generation || request.generation >= UINT32_MAX ||
        kind != track_sharing::UploadKind::Kml) return Status::InvalidField;
    if (!session_matches) return Status::Unauthorized;
    if (!author_hash.data || author_hash.size != 32 || !generation || generation >= UINT32_MAX ||
        request.generation != generation) return Status::StaleIdentity;
    if (!media_ready) return Status::StorageUnavailable;
    if (!track_sharing::uploadJournalValid(journal)) return Status::InvalidField;
    if (journal.generation != generation || std::memcmp(journal.author_hash, author_hash.data, 32)) return Status::StaleIdentity;
    if (journal.phase != track_sharing::UploadPhase::Sealed || journal.total != request.bytes ||
        std::memcmp(journal.id, request.transfer.data, 16)) return Status::Conflict;
    if (std::memcmp(journal.file_hash, request.hash.data, 32)) return Status::HashMismatch;
    return Status::Ok;
}
} // namespace hostlink::management
