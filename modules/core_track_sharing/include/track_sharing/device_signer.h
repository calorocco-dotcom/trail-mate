#pragma once
#include "chat/ports/device_author.h"
#include "track_sharing/sign_manifest.h"
namespace track_sharing
{
// Shared implementation of the device signing port. Publication authorization
// remains the caller's responsibility. No identity/export workspace is owned.
template <class Identity>
chat::DeviceAuthorResult signDeviceTrackManifest(Identity& identity, const uint8_t* raw, size_t size, uint32_t generation, ManifestView& view,
                                                 uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity)
{
    using Result = chat::DeviceAuthorResult;
    if (!raw || !size || size > 4096 || !generation || !workspace || capacity < sizeof("TrailMate.Track.Manifest.v1") + size || !signature || signature_capacity < 64 ||
        signingSpansOverlap(raw, size, workspace, capacity) || signingSpansOverlap(raw, size, signature, 64) ||
        signingSpansOverlap(workspace, capacity, signature, 64) || signingSpansOverlap(raw, size, reinterpret_cast<const uint8_t*>(&view), sizeof(view))) return Result::InvalidInput;
    std::memset(signature, 0, 64);
    if (!identity.isReady()) return identity.identityStorageError() ? Result::StorageError : Result::Unavailable;
    if (!identity.authorGeneration()) return Result::StorageError;
    if (identity.authorGeneration() != generation) return Result::StaleIdentity;
    if (signManifestForGeneration({raw, size}, identity, generation, view, workspace, capacity, signature, signature_capacity)) return Result::Ok;
    return identity.authorGeneration() != generation ? Result::StaleIdentity : Result::InvalidInput;
}
} // namespace track_sharing
