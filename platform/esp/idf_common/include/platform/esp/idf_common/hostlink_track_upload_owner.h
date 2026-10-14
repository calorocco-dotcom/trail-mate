#pragma once
#include "hostlink/management_envelope.h"
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/upload_protocol.h"
#include "track_sharing/upload_resources.h"
#include "track_sharing/upload_session.h"
#include "track_sharing/upload_space_budget.h"
#include <esp_heap_caps.h>
#include <new>
namespace hostlink::management::upload_runtime
{
namespace sd = platform::esp::arduino_common::storage;
// For new transactions only. Recovery reserves no additional files. Every
// unknown/orphan entry counts; only a verified End completes admission.
inline Status admitUploadSpace(uint32_t media, uint32_t bytes, uint8_t* scratch, size_t capacity, track_sharing::UploadKind kind = track_sharing::UploadKind::Kml)
{
    if (!scratch || capacity < 128) return Status::Internal;
    if (!bytes || bytes > track_sharing::uploadByteLimit(kind)) return Status::InvalidField;
    sd::SdRuntimeDir directory(sd::SdRuntimeMemoryPolicy::PsramOnly);
    if (!directory.has_backing_storage()) return Status::Busy;
    const auto opened = directory.open_read_status(track_sharing::uploadDirectory(kind), media);
    if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    track_sharing::UploadSpaceBudget budget(bytes, kind);
    for (unsigned scan = 0; scan < 193; ++scan)
    {
        bool is_dir = false;
        uint64_t size = 0, next = 0;
        const auto result = directory.read_next_info(reinterpret_cast<char*>(scratch + capacity - 128), 128, &is_dir, &size, &next);
        if (result == sd::SdDirReadStatus::Busy) return Status::Busy;
        if (result == sd::SdDirReadStatus::End)
            return media == sd::sd_media_session() && !sd::sd_external_block_owner_active() ? Status::Ok : Status::StorageUnavailable;
        if (result != sd::SdDirReadStatus::Entry) return Status::StorageUnavailable;
        if (!budget.add(size, is_dir)) return Status::LimitExceeded;
    }
    return Status::LimitExceeded;
}
class TrackUploadOwner
{
  public:
    using Resources = track_sharing::UploadResources<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256>;
    TrackUploadOwner(const uint8_t* id, const uint8_t* session_id, uint32_t media_id, uint32_t identity_generation, const uint8_t* identity_hash, uint64_t now, track_sharing::UploadKind upload_kind = track_sharing::UploadKind::Kml)
        : media(media_id), generation(identity_generation), kind(upload_kind), file(sd::SdRuntimeMemoryPolicy::PsramOnly), io(file, media), hash(context), resources(io, hash, id, generation, author, kind), session(resources, session_id, media, now)
    {
        std::memcpy(author, identity_hash, 32);
    }
    TrackUploadOwner(const TrackUploadOwner&) = delete;
    TrackUploadOwner& operator=(const TrackUploadOwner&) = delete;
    // Platform refreshes these from authenticated current identity before use.
    uint32_t media, generation;
    const track_sharing::UploadKind kind;
    bool discarding = false;
    uint8_t author[32]{};
    mbedtls_sha256_context context;
    sd::SdRuntimeFile file;
    sd::SdBorrowedRecordFileIo io;
    platform::esp::storage::SnapshotStreamSha256 hash;
    Resources resources;
    track_sharing::UploadSession<Resources> session;
};
static_assert(sizeof(TrackUploadOwner) <= 1024, "Upload owner exceeds transient PSRAM admission");
// Scheduler owns the returned pointer; no global slot/queue is added here.
// SD driver's separately allocated Impl must also be counted in admission.
inline TrackUploadOwner* createOwner(const uint8_t* id, const uint8_t* session, uint32_t media, uint32_t generation, const uint8_t* author, uint64_t now, track_sharing::UploadKind kind = track_sharing::UploadKind::Kml)
{
    if (!track_sharing::uploadByteLimit(kind) || !id || !session || !author || !generation || media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    uint8_t id_bits = 0, session_bits = 0, author_bits = 0;
    for (unsigned i = 0; i < 16; ++i)
    {
        id_bits |= id[i];
        session_bits |= session[i];
    }
    for (unsigned i = 0; i < 32; ++i) author_bits |= author[i];
    if (!id_bits || !session_bits || !author_bits) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackUploadOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* created = new (memory) TrackUploadOwner(id, session, media, generation, author, now, kind);
    if (!created->file.has_backing_storage())
    {
        created->~TrackUploadOwner();
        heap_caps_free(memory);
        return nullptr;
    }
    return created;
}
inline void destroyOwner(TrackUploadOwner* owner)
{
    if (!owner) return;
    owner->resources.cancel();
    owner->~TrackUploadOwner();
    heap_caps_free(owner);
}
// Caller holds exclusive scheduler/storage lease. Request fields must live
// outside borrowed scratch. Recovery is a distinct path, never truncation.
inline Status beginNewUpload(TrackUploadOwner*& output, const uint8_t* id, const uint8_t* session, uint32_t media,
                             uint32_t generation, const uint8_t* author, uint32_t bytes, const uint8_t* hash,
                             uint64_t now, uint8_t* scratch, size_t capacity, track_sharing::UploadKind kind = track_sharing::UploadKind::Kml)
{
    if (output) return Status::Busy;
    if (!id || !session || !author || !hash || !generation || !bytes || bytes > track_sharing::uploadByteLimit(kind) || !scratch || capacity < 128) return Status::InvalidField;
    const auto overlaps = [&](const uint8_t* input, size_t length)
    { const auto a = reinterpret_cast<uintptr_t>(input), b = reinterpret_cast<uintptr_t>(scratch);return a <= b ? b - a < length : a - b < capacity; };
    if (overlaps(id, 16) || overlaps(session, 16) || overlaps(author, 32) || overlaps(hash, 32)) return Status::InvalidField;
    uint8_t id_bits = 0, session_bits = 0, author_bits = 0;
    for (unsigned i = 0; i < 16; ++i)
    {
        id_bits |= id[i];
        session_bits |= session[i];
    }
    for (unsigned i = 0; i < 32; ++i) author_bits |= author[i];
    if (!id_bits || !session_bits || !author_bits) return Status::InvalidField;
    if (media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    const char* directory = track_sharing::uploadDirectory(kind);
    if ((!sd::sd_is_directory("/trailmate") && !sd::sd_mkdir("/trailmate")) ||
        (!sd::sd_is_directory(directory) && !sd::sd_mkdir(directory))) return Status::StorageUnavailable;
    const auto admission = admitUploadSpace(media, bytes, scratch, capacity, kind);
    if (admission != Status::Ok) return admission;
    auto* owner = createOwner(id, session, media, generation, author, now, kind);
    if (!owner) return Status::Busy;
    const auto fail = [&](Status status)
    { destroyOwner(owner);return status; };
    const auto recovered = owner->resources.recover(scratch, capacity);
    if (recovered != track_sharing::JournalStoreResult::Missing)
        return fail(recovered == track_sharing::JournalStoreResult::Ready || recovered == track_sharing::JournalStoreResult::Conflict ? Status::Conflict : Status::StorageUnavailable);
    const auto existing = owner->io.open(owner->resources.candidatePath(), sd::SdBorrowedRecordFileIo::Mode::Read);
    if (existing.handle)
    {
        const bool closed = owner->io.close(existing.handle);
        return fail(closed ? Status::Conflict : Status::StorageUnavailable);
    }
    if (existing.status != sd::SdBorrowedRecordFileIo::OpenStatus::Missing) return fail(Status::StorageUnavailable);
    const auto created = owner->io.open(owner->resources.candidatePath(), sd::SdBorrowedRecordFileIo::Mode::CreateTemporary);
    if (!created.handle) return fail(Status::StorageUnavailable);
    const bool synced = owner->io.sync(created.handle), closed = owner->io.close(created.handle);
    if (!synced || !closed) return fail(Status::StorageUnavailable);
    const auto initialized = owner->resources.initialize(bytes, hash, scratch, capacity);
    if (initialized != track_sharing::JournalStoreResult::Ready) return fail(initialized == track_sharing::JournalStoreResult::Conflict ? Status::Conflict : Status::StorageUnavailable);
    output = owner;
    return Status::Ok;
}
// Recovery reads only; restored Sealed metadata is not fresh file proof.
inline Status recoverUpload(TrackUploadOwner*& output, const uint8_t* id, const uint8_t* session, uint32_t media,
                            uint32_t generation, const uint8_t* author, uint32_t bytes, const uint8_t* hash,
                            uint64_t now, uint8_t* scratch, size_t capacity, track_sharing::UploadKind kind = track_sharing::UploadKind::Kml)
{
    if (output) return Status::Busy;
    if (!id || !session || !author || !hash || !generation || !bytes || bytes > track_sharing::uploadByteLimit(kind) || !scratch || capacity < 128) return Status::InvalidField;
    const auto overlaps = [&](const uint8_t* input, size_t length)
    { const auto a = reinterpret_cast<uintptr_t>(input), b = reinterpret_cast<uintptr_t>(scratch);return a <= b ? b - a < length : a - b < capacity; };
    if (overlaps(id, 16) || overlaps(session, 16) || overlaps(author, 32) || overlaps(hash, 32)) return Status::InvalidField;
    uint8_t id_bits = 0, session_bits = 0, author_bits = 0;
    for (unsigned i = 0; i < 16; ++i)
    {
        id_bits |= id[i];
        session_bits |= session[i];
    }
    for (unsigned i = 0; i < 32; ++i) author_bits |= author[i];
    if (!id_bits || !session_bits || !author_bits) return Status::InvalidField;
    if (media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    auto* owner = createOwner(id, session, media, generation, author, now, kind);
    if (!owner) return Status::Busy;
    const auto fail = [&](Status status)
    { destroyOwner(owner);return status; };
    const auto loaded = owner->resources.recover(scratch, capacity);
    if (loaded != track_sharing::JournalStoreResult::Ready)
        return fail(loaded == track_sharing::JournalStoreResult::Missing ? Status::Expired : loaded == track_sharing::JournalStoreResult::Conflict ? Status::Conflict
                                                                                                                                                   : Status::StorageUnavailable);
    const auto& metadata = owner->resources.metadata();
    if (metadata.total != bytes || std::memcmp(metadata.file_hash, hash, 32)) return fail(Status::Conflict);
    if (metadata.phase == track_sharing::UploadPhase::Failed)
    {
        owner->discarding = true;
        output = owner;
        return Status::HashMismatch;
    }
    const auto opened = owner->io.open(owner->resources.candidatePath(), sd::SdBorrowedRecordFileIo::Mode::Read);
    if (!opened.handle)
    {
        if (opened.status == sd::SdBorrowedRecordFileIo::OpenStatus::Missing)
        {
            owner->discarding = true;
            output = owner;
            return Status::StorageUnavailable;
        }
        return fail(Status::StorageUnavailable);
    }
    uint64_t size = 0;
    const bool sized = owner->io.size(opened.handle, size), closed = owner->io.close(opened.handle);
    if (!sized || !closed) return fail(Status::StorageUnavailable);
    if (size < metadata.durable_offset || size > bytes)
    {
        owner->discarding = true;
        output = owner;
        return Status::StorageUnavailable;
    }
    output = owner;
    return Status::Ok;
}
inline Status abortOwnedUpload(TrackUploadOwner& owner, const uint8_t* session, const uint8_t* id, uint64_t now, uint8_t* scratch, size_t capacity)
{
    if (!id || !scratch || capacity < 128) return Status::InvalidField;
    const auto& metadata = owner.resources.metadata();
    if (std::memcmp(id, metadata.id, 16)) return Status::Conflict;
    if (owner.generation != metadata.generation || std::memcmp(owner.author, metadata.author_hash, 32)) return Status::StaleIdentity;
    if (!owner.session.authorize(session, sd::sd_media_session(), now)) return Status::Unauthorized;
    owner.discarding = true;
    owner.resources.cancel();
    for (const auto file : {track_sharing::UploadFile::Candidate, track_sharing::UploadFile::JournalA, track_sharing::UploadFile::JournalB})
    {
        auto* path = reinterpret_cast<char*>(scratch);
        if (!track_sharing::resolveUploadPath(metadata.id, 16, file, path, capacity, owner.kind)) return Status::Internal;
        const auto existing = owner.io.open(path, sd::SdBorrowedRecordFileIo::Mode::Read);
        if (!existing.handle)
        {
            if (existing.status == sd::SdBorrowedRecordFileIo::OpenStatus::Missing) continue;
            return Status::StorageUnavailable;
        }
        if (!owner.io.close(existing.handle) || !sd::sd_remove(path, owner.media)) return Status::StorageUnavailable;
    }
    return Status::Ok;
}
inline Status processExistingUpload(TrackUploadOwner& owner, track_sharing::UploadCommand command,
                                    geocaching::ByteView body, const uint8_t* session, uint64_t now,
                                    track_sharing::UploadRequest& decoded, uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    if (owner.discarding) return Status::Conflict;
    if (!scratch || capacity < 448) return Status::Internal;
    if (!track_sharing::decodeUploadRequest(command, body, decoded)) return Status::InvalidField;
    const auto& metadata = owner.resources.metadata();
    if (std::memcmp(decoded.id.data, metadata.id, 16)) return Status::Conflict;
    if (owner.generation != metadata.generation || std::memcmp(owner.author, metadata.author_hash, 32)) return Status::StaleIdentity;
    if (!owner.session.matches(session, sd::sd_media_session(), now)) return Status::Unauthorized;
    if (!sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    if (metadata.phase == track_sharing::UploadPhase::Failed) return Status::HashMismatch;
    const auto state = owner.resources.sealResult();
    if (state == track_sharing::UploadSealResult::StorageFailure) return Status::StorageUnavailable;
    if (state == track_sharing::UploadSealResult::Cancelled) return Status::Expired;
    if (state == track_sharing::UploadSealResult::Conflict) return Status::StaleIdentity;
    if (command == track_sharing::UploadCommand::Begin)
    {
        if (decoded.kind != owner.kind || decoded.total != metadata.total || std::memcmp(decoded.hash.data, metadata.file_hash, 32)) return Status::Conflict;
    }
    else if (command == track_sharing::UploadCommand::Chunk)
    {
        uint8_t id[16];
        std::memcpy(id, decoded.id.data, 16);
        const auto count = decoded.chunk.size;
        auto* chunk = scratch + capacity - count;
        std::memmove(chunk, decoded.chunk.data, count);
        const auto result = owner.session.append(session, owner.media, now, id, decoded.offset, chunk, count, scratch, capacity);
        if (result != track_sharing::JournalStoreResult::Ready)
            return result == track_sharing::JournalStoreResult::Conflict ? Status::Conflict : result == track_sharing::JournalStoreResult::Invalid ? Status::InvalidField
                                                                                                                                                   : Status::StorageUnavailable;
    }
    else if (command == track_sharing::UploadCommand::Finish && metadata.phase != track_sharing::UploadPhase::Sealed && !owner.resources.sealingStarted())
    {
        const auto result = owner.session.beginSeal(session, owner.media, now, scratch, capacity);
        if (result != track_sharing::UploadSealResult::Pending)
            return result == track_sharing::UploadSealResult::Conflict ? Status::Conflict : result == track_sharing::UploadSealResult::Invalid ? Status::InvalidField
                                                                                                                                               : Status::StorageUnavailable;
    }
    if (!owner.session.authorize(session, owner.media, now)) return Status::Unauthorized;
    return track_sharing::encodeUploadProgress(metadata, scratch, capacity, written) ? Status::Ok : Status::Internal;
}
} // namespace hostlink::management::upload_runtime
