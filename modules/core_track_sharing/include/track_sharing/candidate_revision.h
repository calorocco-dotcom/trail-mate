#pragma once
#include "track_sharing/manifest_chain.h"
#include "track_sharing/verify_manifest.h"
namespace track_sharing
{
// Parent must be the exact verified revision selected by durable reservation.
// This checks a local unsigned candidate; it does not reserve a revision or
// authorize signing. Caller retains immutable manifest/parent storage.
inline bool validateCandidateRevision(geocaching::ByteView raw, geocaching::ByteView device_author,
                                      const VerifiedManifestView* parent, ManifestView& candidate)
{
    if (!device_author.data || device_author.size != 64 || (parent && (!parent->verified || &parent->manifest == &candidate)) ||
        signingSpansOverlap(raw.data, raw.size, reinterpret_cast<const uint8_t*>(&candidate), sizeof(candidate)) || !decodeManifest(raw, candidate)) return false;
    if (std::memcmp(candidate.author.data, device_author.data, 64)) return false;
    if (!parent) return candidate.revision == 1 && candidate.state == 0 && !candidate.previous.size;
    return validateSuccessor(parent->manifest, candidate, parent->revision_hash);
}
} // namespace track_sharing
