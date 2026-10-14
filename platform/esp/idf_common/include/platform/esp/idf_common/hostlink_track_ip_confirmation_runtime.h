#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/idf_common/hostlink_track_ip_confirmation_owner.h"
namespace hostlink::management::ip_confirmation_runtime
{
// One resident pointer. Request copy, session/timer and work live in PSRAM.
// Repeating the SAME request advances one bounded step. SessionClose releases;
// a new session can recover persisted evidence after a lost reply/disconnect.
struct Controller
{
    TrackIpConfirmationOwner* work = nullptr;
    track_sharing::IpConfirmationRequest decoded;
    uint8_t raw[416]{}, session[16]{};
    size_t size = 0;
    uint32_t media = 0;
    uint64_t last = 0;
    Status status = Status::Busy;
    uint16_t operation = 0;
};
static_assert(sizeof(Controller) <= 768, "Confirmation controller must remain bounded PSRAM metadata");
inline Controller* controller = nullptr;
inline void release()
{
    if (!controller) return;
    destroyOwner(controller->work);
    controller->~Controller();
    heap_caps_free(controller);
    controller = nullptr;
}
inline void closeSession(const uint8_t* session)
{
    if (controller && session && !std::memcmp(session, controller->session, 16)) release();
}
inline void tick(uint64_t now, bool connected)
{
    if (controller && (!connected || controller->media != sd::sd_media_session() || now < controller->last || now - controller->last >= 30000)) release();
}
inline bool holdsStorage() { return controller && controller->work && controller->status == Status::Busy; }
constexpr bool blocksOperation(uint16_t operation)
{
    switch (static_cast<Operation>(operation))
    {
    case Operation::Capabilities:
    case Operation::AuthorInfo:
    case Operation::AuthorProve:
    case Operation::SessionClose:
    case Operation::TrackConfirmPublication:
    case Operation::TrackImportHistory:
        return false;
    default:
        return true;
    }
}
inline Status processConfirmation(chat::IMeshAdapter& adapter, const EnvelopeView& request, uint64_t now,
                                  uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    const bool import_history = request.code == uint16_t(Operation::TrackImportHistory);
    if (!request.session || !request.body || !request.body_size || request.body_size > (import_history ? 416 : 288) || !scratch || capacity < 448) return Status::InvalidField;
    tick(now, true);
    // A completed response stays available for exact retries. The owning
    // session may start its next publication without waiting for the TTL.
    if (controller && controller->status == Status::Ok && !std::memcmp(controller->session, request.session, 16) &&
        (controller->operation != request.code || controller->size != request.body_size || std::memcmp(controller->raw, request.body, request.body_size))) release();
    if (controller && (controller->operation != request.code || std::memcmp(controller->session, request.session, 16) || controller->size != request.body_size ||
                       std::memcmp(controller->raw, request.body, request.body_size))) return Status::Conflict;
    if (!controller)
    {
        void* memory = heap_caps_malloc(sizeof(Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return Status::Busy;
        controller = new (memory) Controller;
        std::memcpy(controller->session, request.session, 16);
        std::memcpy(controller->raw, request.body, request.body_size);
        controller->size = request.body_size;
        controller->media = sd::sd_media_session();
        controller->last = now;
        controller->operation = request.code;
        if (!track_sharing::decodeIpConfirmationRequest({controller->raw, controller->size}, controller->decoded, import_history))
        {
            release();
            return Status::InvalidField;
        }
    }
    if (!controller->work)
    {
        auto* key = scratch + capacity - 64;
        uint8_t author_hash[32];
        uint32_t generation = 0;
        const auto identity = upload_runtime::readUploadAuthor(adapter, key, 64, author_hash, generation);
        if (identity != Status::Ok)
        {
            controller->status = identity;
            return identity;
        }
        if (generation != controller->decoded.generation)
        {
            controller->status = Status::StaleIdentity;
            return controller->status;
        }
        if (upload_runtime::owner)
        {
            const auto& upload = upload_runtime::owner->resources.metadata();
            if (upload_runtime::owner->kind != track_sharing::UploadKind::IpReceipt || upload.phase != track_sharing::UploadPhase::Sealed ||
                upload.generation != generation || std::memcmp(upload.id, controller->decoded.transfer.data, 16) ||
                std::memcmp(upload.file_hash, controller->decoded.proof_hash.data, 32) || std::memcmp(upload.author_hash, author_hash, 32) ||
                !upload_runtime::owner->session.matches(controller->session, controller->media, now))
            {
                release();
                return Status::Busy;
            }
            upload_runtime::destroyOwner(upload_runtime::owner);
            upload_runtime::owner = nullptr;
        }
        controller->work = createOwner(controller->decoded, controller->media, key, author_hash);
        if (!controller->work) return Status::Busy;
    }
    // Identity/media is rechecked even when returning a completed result.
    controller->status = controller->work->advance(adapter, scratch, capacity);
    if (controller->status != Status::Ok && controller->status != Status::Busy) return controller->status;
    controller->last = now;
    const auto* result = controller->work->result();
    // A completed retry can follow a newly resealed proof upload. Release only
    // the exact proof owned by this current identity/session so bound cleanup
    // can finish; keep unrelated, receiving, or mismatched transfers intact.
    if (result && upload_runtime::owner)
    {
        auto* key = scratch + capacity - 64;
        uint8_t author_hash[32];
        uint32_t generation = 0;
        const auto identity = upload_runtime::readUploadAuthor(adapter, key, 64, author_hash, generation);
        if (identity != Status::Ok) return identity;
        if (generation != controller->decoded.generation) return Status::StaleIdentity;
        const auto& upload = upload_runtime::owner->resources.metadata();
        const bool proof_matches =
            (upload_runtime::owner->kind == track_sharing::UploadKind::IpReceipt &&
             !std::memcmp(upload.id, controller->decoded.transfer.data, 16) &&
             !std::memcmp(upload.file_hash, controller->decoded.proof_hash.data, 32)) ||
            (import_history && upload_runtime::owner->kind == track_sharing::UploadKind::Manifest &&
             !std::memcmp(upload.id, controller->decoded.manifest_transfer.data, 16) &&
             !std::memcmp(upload.file_hash, controller->decoded.manifest_hash.data, 32));
        if (proof_matches && !upload_runtime::owner->discarding && upload.phase == track_sharing::UploadPhase::Sealed &&
            upload.generation == generation && !std::memcmp(upload.author_hash, author_hash, 32) &&
            upload_runtime::owner->session.matches(request.session, controller->media, now))
        {
            upload_runtime::destroyOwner(upload_runtime::owner);
            upload_runtime::owner = nullptr;
        }
    }
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    if (!writer.array(8) || !writer.binary(controller->decoded.operation) || !writer.binary(controller->decoded.transfer) ||
        !writer.unsignedInteger(controller->decoded.generation) || !writer.unsignedInteger(result ? 1 : 0) ||
        !(result ? writer.binary({result->object, 32}) : writer.nil()) || !(result ? writer.binary({result->hash, 32}) : writer.nil()) ||
        !writer.unsignedInteger(result ? result->revision : 0) || !writer.unsignedInteger(result ? 1 : 0)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace hostlink::management::ip_confirmation_runtime
