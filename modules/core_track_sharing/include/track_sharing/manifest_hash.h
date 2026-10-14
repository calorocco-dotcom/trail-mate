#pragma once
#include "track_sharing/sign_manifest.h"
namespace track_sharing
{
// Hash lends start/update/finish/reset; it owns its separately admitted context.
// No domain+manifest allocation, no automatic ManifestView. Schema validity
// and hashes do not establish author authenticity or file availability.
template <class Hash>
bool deriveManifestHashes(geocaching::ByteView raw, ManifestView& view, Hash& hash, uint8_t object_id[32], uint8_t revision_hash[32])
{
    if (!object_id || !revision_hash || signingSpansOverlap(raw.data, raw.size, object_id, 32) ||
        signingSpansOverlap(raw.data, raw.size, revision_hash, 32) || signingSpansOverlap(object_id, 32, revision_hash, 32)) return false;
    std::memset(object_id, 0, 32);
    std::memset(revision_hash, 0, 32);
    if (!decodeManifest(raw, view)) return false;
    constexpr char object_domain[] = "TrailMate.Track.ID.v1";
    constexpr char revision_domain[] = "TrailMate.Track.Revision.v1";
    constexpr char revision_v2_domain[] = "TrailMate.Track.Revision.v2";
    const bool ok = hash.start() && hash.update(reinterpret_cast<const uint8_t*>(object_domain), sizeof(object_domain)) &&
                    hash.update(view.author.data, view.author.size) && hash.update(view.nonce.data, view.nonce.size) && hash.finish(object_id) &&
                    hash.start() && hash.update(reinterpret_cast<const uint8_t*>(view.version == 2 ? revision_v2_domain : revision_domain), sizeof(revision_domain)) &&
                    hash.update(raw.data, raw.size) && hash.finish(revision_hash);
    hash.reset();
    if (!ok)
    {
        std::memset(object_id, 0, 32);
        std::memset(revision_hash, 0, 32);
    }
    return ok;
}
} // namespace track_sharing
