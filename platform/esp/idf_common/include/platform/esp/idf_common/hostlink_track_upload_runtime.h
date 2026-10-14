#pragma once
#include "platform/esp/idf_common/hostlink_track_upload_owner.h"
namespace hostlink::management::upload_runtime
{
// Single application-scheduler owner, not a CDC task/queue. Caller supplies
// authenticated current author hash outside command scratch before each use.
inline TrackUploadOwner* owner = nullptr;
inline bool routeTrackUpload(const EnvelopeView& request)
{
    if (request.code == static_cast<uint16_t>(Operation::TransferBegin))
    {
        track_sharing::UploadRequest decoded;
        return track_sharing::decodeUploadRequest(track_sharing::UploadCommand::Begin, {request.body, request.body_size}, decoded);
    }
    if (!owner || (request.code != static_cast<uint16_t>(Operation::TransferChunk) && request.code != static_cast<uint16_t>(Operation::TransferFinish) && request.code != static_cast<uint16_t>(Operation::TransferAbort) && request.code != static_cast<uint16_t>(Operation::TransferStatus))) return false;
    track_sharing::UploadRequest decoded;
    const auto command = request.code == static_cast<uint16_t>(Operation::TransferChunk) ? track_sharing::UploadCommand::Chunk : request.code == static_cast<uint16_t>(Operation::TransferStatus) ? track_sharing::UploadCommand::Status
                                                                                                                                                                                                  : track_sharing::UploadCommand::Finish;
    return track_sharing::decodeUploadRequest(command, {request.body, request.body_size}, decoded) && !std::memcmp(decoded.id.data, owner->resources.metadata().id, 16);
}
inline void advanceUploads(uint8_t* scratch, size_t capacity, uint64_t now, bool connected)
{
    if (!owner) return;
    owner->session.advance(sd::sd_media_session(), now, connected, scratch, capacity);
    if (owner->session.releasable())
    {
        destroyOwner(owner);
        owner = nullptr;
    }
}
inline void closeUploadSession(const uint8_t* session)
{
    if (!owner || !owner->session.close(session)) return;
    destroyOwner(owner);
    owner = nullptr;
}
inline Status processUploadRequest(const EnvelopeView& request, uint32_t generation, const uint8_t* author,
                                   uint64_t now, uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    if (!request.session || !author || !generation) return Status::Unauthorized;
    if (!scratch || capacity < 448) return Status::Internal;
    track_sharing::UploadCommand command;
    if (request.code == static_cast<uint16_t>(Operation::TransferBegin)) command = track_sharing::UploadCommand::Begin;
    else if (request.code == static_cast<uint16_t>(Operation::TransferChunk)) command = track_sharing::UploadCommand::Chunk;
    else if (request.code == static_cast<uint16_t>(Operation::TransferStatus)) command = track_sharing::UploadCommand::Status;
    else if (request.code == static_cast<uint16_t>(Operation::TransferFinish) || request.code == static_cast<uint16_t>(Operation::TransferAbort)) command = track_sharing::UploadCommand::Finish;
    else return Status::Unsupported;
    track_sharing::UploadRequest decoded;
    if (!track_sharing::decodeUploadRequest(command, {request.body, request.body_size}, decoded)) return Status::InvalidField;
    if (owner && command == track_sharing::UploadCommand::Begin && std::memcmp(decoded.id.data, owner->resources.metadata().id, 16))
    {
        const auto& metadata = owner->resources.metadata();
        if (owner->discarding || metadata.phase != track_sharing::UploadPhase::Sealed) return Status::Conflict;
        if (generation != metadata.generation || std::memcmp(author, metadata.author_hash, 32)) return Status::StaleIdentity;
        if (!owner->session.matches(request.session, sd::sd_media_session(), now)) return Status::Unauthorized;
        if (!sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
        // Retire only transient resources. Sealed bytes and journals remain
        // available for preparation or explicit recovery/cleanup.
        destroyOwner(owner);
        owner = nullptr;
    }
    if (owner)
    {
        // Refresh borrowed identity fields before every operation. A changed
        // identity cannot keep uploading bytes under the journal's old binding.
        owner->generation = generation;
        std::memcpy(owner->author, author, 32);
        if (request.code == static_cast<uint16_t>(Operation::TransferAbort))
        {
            const auto result = abortOwnedUpload(*owner, request.session, decoded.id.data, now, scratch, capacity);
            if (result != Status::Ok) return result;
            destroyOwner(owner);
            owner = nullptr;
            scratch[0] = 0x90;
            written = 1;
            return Status::Ok;
        }
        return processExistingUpload(*owner, command, {request.body, request.body_size}, request.session, now, decoded, scratch, capacity, written);
    }
    if (command != track_sharing::UploadCommand::Begin) return Status::Expired;
    // Small binding copy only; file data always remains in the command arena.
    uint8_t bindings[48];
    std::memcpy(bindings, decoded.id.data, 16);
    std::memcpy(bindings + 16, decoded.hash.data, 32);
    const auto recovered = recoverUpload(owner, bindings, request.session, sd::sd_media_session(), generation, author, decoded.total, bindings + 16, now, scratch, capacity, decoded.kind);
    if (recovered != Status::Ok && recovered != Status::Expired) return recovered;
    if (recovered == Status::Expired)
    {
        const auto created = beginNewUpload(owner, bindings, request.session, sd::sd_media_session(), generation, author, decoded.total, bindings + 16, now, scratch, capacity, decoded.kind);
        if (created != Status::Ok) return created;
    }
    return track_sharing::encodeUploadProgress(owner->resources.metadata(), scratch, capacity, written) ? Status::Ok : Status::Internal;
}
} // namespace hostlink::management::upload_runtime
