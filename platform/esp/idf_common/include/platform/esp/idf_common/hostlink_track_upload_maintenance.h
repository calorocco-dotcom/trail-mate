#pragma once
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
namespace hostlink::management::upload_runtime
{
// Exact binding cleanup does not require an intact candidate. The two durable
// journals prove ownership even after an interrupted deletion or disconnect.
inline Status cleanupBoundUpload(const EnvelopeView& request, uint32_t generation, const uint8_t* author,
                                 uint64_t now, uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    track_sharing::UploadRequest decoded;
    if (!request.session || !author || !generation || !scratch || capacity < 448 ||
        !track_sharing::decodeUploadRequest(track_sharing::UploadCommand::Begin, {request.body, request.body_size}, decoded)) return Status::InvalidField;
    uint8_t binding[48];
    std::memcpy(binding, decoded.id.data, 16);
    std::memcpy(binding + 16, decoded.hash.data, 32);
    const auto kind = decoded.kind;
    const auto total = decoded.total;
    const auto media = sd::sd_media_session();
    bool created = false;
    if (!sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    if (owner && std::memcmp(binding, owner->resources.metadata().id, 16)) return Status::Busy;
    if (!owner)
    {
        owner = createOwner(binding, request.session, media, generation, author, now, kind);
        if (!owner) return Status::Busy;
        created = true;
        const auto loaded = owner->resources.recover(scratch, capacity);
        if (loaded != track_sharing::JournalStoreResult::Ready)
        {
            Status result = Status::Conflict;
            if (loaded == track_sharing::JournalStoreResult::Missing)
            {
                // Missing journals alone are not permission to erase an orphan.
                const auto candidate = owner->io.open(owner->resources.candidatePath(), sd::SdBorrowedRecordFileIo::Mode::Read);
                if (candidate.handle)
                {
                    if (!owner->io.close(candidate.handle)) result = Status::StorageUnavailable;
                }
                else result = candidate.status == sd::SdBorrowedRecordFileIo::OpenStatus::Missing ? Status::Ok : Status::StorageUnavailable;
            }
            else if (loaded == track_sharing::JournalStoreResult::StorageFailure) result = Status::StorageUnavailable;
            destroyOwner(owner);
            owner = nullptr;
            if (media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) result = Status::StorageUnavailable;
            if (result == Status::Ok)
            {
                scratch[0] = 0x90;
                written = 1;
            }
            return result;
        }
    }
    const auto& metadata = owner->resources.metadata();
    const auto reject = [&](Status status)
    { if (created) { destroyOwner(owner); owner = nullptr; } return status; };
    if (kind != owner->kind || total != metadata.total || std::memcmp(binding + 16, metadata.file_hash, 32)) return reject(Status::Conflict);
    if (generation != metadata.generation || std::memcmp(author, metadata.author_hash, 32)) return reject(Status::StaleIdentity);
    if (!owner->session.matches(request.session, media, now)) return reject(Status::Unauthorized);
    owner->generation = generation;
    std::memcpy(owner->author, author, 32);
    const auto result = abortOwnedUpload(*owner, request.session, binding, now, scratch, capacity);
    if (result != Status::Ok) return result;
    destroyOwner(owner);
    owner = nullptr;
    if (media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    scratch[0] = 0x90;
    written = 1;
    return Status::Ok;
}
inline bool parseUploadJournalName(const char* name, uint8_t* id)
{
    if (!name || !id || std::strlen(name) != 36 || (std::strcmp(name + 32, ".uja") && std::strcmp(name + 32, ".ujb"))) return false;
    uint8_t bits = 0;
    for (unsigned i = 0; i < 16; ++i)
    {
        const auto digit = [](char value)
        { return value >= '0' && value <= '9' ? value - '0' : value >= 'a' && value <= 'f' ? value - 'a' + 10
                                                                                           : -1; };
        const int high = digit(name[i * 2]), low = digit(name[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        id[i] = uint8_t(high * 16 + low);
        bits |= id[i];
    }
    return bits != 0;
}
// [kind,null|[media,position]] -> [media,next|null,row|null]. Live catalogue,
// four directory entries per request, at most one row. Both journal names may
// yield the same id; callers deduplicate. No candidate bytes are read or sealed.
inline Status listOwnedUploads(const EnvelopeView& request, uint32_t generation, const uint8_t* author,
                               uint64_t now, uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    if (!request.session || !author || !generation || !scratch || capacity < 448 || owner) return owner ? Status::Busy : Status::InvalidField;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t raw_kind = 0, wanted = 0, position = 0;
    if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(raw_kind) || raw_kind > UINT8_MAX || !track_sharing::uploadByteLimit(static_cast<track_sharing::UploadKind>(raw_kind))) return Status::InvalidField;
    const bool first = reader.position() < request.body_size && request.body[reader.position()] == 0xc0;
    if (first)
    {
        if (!reader.nil()) return Status::InvalidField;
    }
    else if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(wanted) || wanted > UINT32_MAX ||
             !reader.unsignedInteger(position) || position > 128U * 1024U * 1024U || position % 32) return Status::InvalidField;
    if (!reader.finished()) return Status::InvalidField;
    const auto kind = static_cast<track_sharing::UploadKind>(raw_kind);
    const auto media = sd::sd_media_session();
    if (!first && wanted != media) return Status::Conflict;
    if (!sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    bool ended = false, found = false;
    uint8_t id[16];
    {
        sd::SdRuntimeDir directory(sd::SdRuntimeMemoryPolicy::PsramOnly);
        if (!directory.has_backing_storage()) return Status::Busy;
        const auto opened = directory.open_read_status(track_sharing::uploadDirectory(kind), media);
        if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
        if (opened == sd::SdFileReadStatus::Missing) ended = true;
        else if (opened != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
        if (!ended && position)
        {
            const auto seek = directory.seek_read_status(position);
            if (seek != sd::SdFileReadStatus::Ready) return seek == sd::SdFileReadStatus::Busy ? Status::Busy : seek == sd::SdFileReadStatus::Invalid ? Status::Conflict
                                                                                                                                                      : Status::StorageUnavailable;
        }
        for (unsigned scan = 0; !ended && !found && scan < 4; ++scan)
        {
            bool is_directory = false;
            uint64_t bytes = 0, next = 0;
            auto* name = reinterpret_cast<char*>(scratch + capacity - 128);
            const auto entry = directory.read_next_info(name, 128, &is_directory, &bytes, &next);
            if (entry == sd::SdDirReadStatus::End)
            {
                ended = true;
                break;
            }
            if (entry != sd::SdDirReadStatus::Entry) return entry == sd::SdDirReadStatus::Busy ? Status::Busy : Status::StorageUnavailable;
            if (next <= position || next % 32 || next > 128U * 1024U * 1024U) return Status::Conflict;
            position = next;
            found = !is_directory && parseUploadJournalName(name, id);
        }
    }
    TrackUploadOwner* temporary = nullptr;
    struct Release
    {
        TrackUploadOwner*& value;
        ~Release() { destroyOwner(value); }
    } release{temporary};
    if (found)
    {
        temporary = createOwner(id, request.session, media, generation, author, now, kind);
        if (!temporary) return Status::Busy;
        const auto loaded = temporary->resources.recover(scratch, capacity);
        if (loaded == track_sharing::JournalStoreResult::Conflict) found = false; // Another identity/generation remains untouched.
        else if (loaded != track_sharing::JournalStoreResult::Ready) return loaded == track_sharing::JournalStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
    }
    if (media != sd::sd_media_session() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    if (!writer.array(3) || !writer.unsignedInteger(media) || !(ended ? writer.nil() : writer.unsignedInteger(position))) return Status::Internal;
    if (found)
    {
        const auto& metadata = temporary->resources.metadata();
        if (!writer.array(7) || !writer.binary({metadata.id, 16}) || !writer.unsignedInteger(raw_kind) || !writer.unsignedInteger(metadata.total) ||
            !writer.binary({metadata.file_hash, 32}) || !writer.unsignedInteger(metadata.generation) || !writer.unsignedInteger(uint8_t(metadata.phase)) ||
            !writer.unsignedInteger(metadata.durable_offset)) return Status::Internal;
    }
    else if (!writer.nil()) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
template <class Adapter>
Status processUploadMaintenance(Adapter& adapter, const EnvelopeView& request, uint64_t now,
                                uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    if (!scratch || capacity < 448 || request.body_size > 128) return Status::InvalidField;
    const auto input = reinterpret_cast<uintptr_t>(request.body), key = reinterpret_cast<uintptr_t>(scratch + capacity - 64);
    if (input <= key ? key - input < request.body_size : input - key < 64) return Status::Internal;
    uint8_t author[32];
    uint32_t generation = 0;
    const auto identity = readUploadAuthor(adapter, scratch + capacity - 64, 64, author, generation);
    if (identity != Status::Ok) return identity;
    if (request.code == uint16_t(Operation::TransferCleanup)) return cleanupBoundUpload(request, generation, author, now, scratch, capacity, written);
    if (request.code == uint16_t(Operation::TransferListOwned)) return listOwnedUploads(request, generation, author, now, scratch, capacity, written);
    return Status::Unsupported;
}
} // namespace hostlink::management::upload_runtime
