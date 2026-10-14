#pragma once
#include "hostlink/management_envelope.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "track_sharing/object_paths.h"
#include "track_sharing/prepare_request.h"
#include "track_sharing/reservation_store.h"
#include <initializer_list>
namespace hostlink::management::prepare_runtime
{
// Caller has admitted this object and holds exclusive media/object lease.
// Unknown I/O is never treated as absence; probe handles close before mkdir.
inline Status ensureObjectDirectory(uint32_t media, geocaching::ByteView object, uint8_t* scratch, size_t capacity)
{
    namespace storage = platform::esp::arduino_common::storage;
    if (!scratch || capacity < track_sharing::kObjectPathCapacity) return Status::Internal;
    const auto ready = [&]()
    { return media == storage::sd_media_session() && storage::sd_card_ready() && !storage::sd_external_block_owner_active(); };
    if (!ready()) return Status::StorageUnavailable;
    auto* object_path = reinterpret_cast<char*>(scratch);
    if (!track_sharing::resolveObjectPath(object, track_sharing::ObjectFile::Directory, {}, object_path, capacity)) return Status::InvalidField;
    const auto probe = [&](const char* path)
    { storage::SdRuntimeDir directory; return directory.open_read_status(path, media); };
    for (const auto* path : {"/trailmate", track_sharing::kObjectDirectory, static_cast<const char*>(object_path)})
    {
        const auto status = probe(path);
        if (!ready()) return Status::StorageUnavailable;
        if (status == storage::SdFileReadStatus::Busy) return Status::Busy;
        if (status == storage::SdFileReadStatus::Ready) continue;
        if (status != storage::SdFileReadStatus::Missing || !storage::sd_mkdir(path) || !ready()) return Status::StorageUnavailable;
        const auto created = probe(path);
        if (created == storage::SdFileReadStatus::Busy) return Status::Busy;
        if (created != storage::SdFileReadStatus::Ready || !ready()) return Status::StorageUnavailable;
    }
    return Status::Ok;
}
// Read-only first-version absence proof under caller's exclusive media/object
// lease. Any orphan/unknown entry blocks initialization; never deletes files.
inline Status requireEmptyObjectDirectory(uint32_t media, geocaching::ByteView object, uint8_t* scratch, size_t capacity)
{
    namespace storage = platform::esp::arduino_common::storage;
    if (!scratch || capacity < 288) return Status::Internal;
    if (media != storage::sd_media_session() || !storage::sd_card_ready() || storage::sd_external_block_owner_active()) return Status::StorageUnavailable;
    auto* path = reinterpret_cast<char*>(scratch);
    if (!track_sharing::resolveObjectPath(object, track_sharing::ObjectFile::Directory, {}, path, 160)) return Status::InvalidField;
    storage::SdRuntimeDir directory;
    const auto opened = directory.open_read_status(path, media);
    if (opened == storage::SdFileReadStatus::Busy) return Status::Busy;
    if (opened == storage::SdFileReadStatus::Ready)
    {
        bool is_directory = false;
        uint64_t bytes = 0, next = 0;
        const auto entry = directory.read_next_info(reinterpret_cast<char*>(scratch + capacity - 128), 128, &is_directory, &bytes, &next);
        if (entry == storage::SdDirReadStatus::Busy) return Status::Busy;
        if (entry == storage::SdDirReadStatus::Entry) return Status::Conflict;
        if (entry != storage::SdDirReadStatus::End) return Status::StorageUnavailable;
    }
    else if (opened != storage::SdFileReadStatus::Missing) return Status::StorageUnavailable;
    return media == storage::sd_media_session() && storage::sd_card_ready() && !storage::sd_external_block_owner_active() ? Status::Ok : Status::StorageUnavailable;
}
// Recovery admission only. Caller has already matched candidate domain hashes
// and verified head absence under the same exclusive object/media lease.
template <class Store>
Status requireOwnedFirstRevisionDirectory(uint32_t media, const track_sharing::PrepareRequest& request,
                                          const uint8_t* author_hash, const track_sharing::RevisionReservation& current,
                                          Store& store, track_sharing::RevisionReservation& verify, uint8_t* scratch, size_t capacity)
{
    namespace storage = platform::esp::arduino_common::storage;
    if (!scratch || capacity < 288) return Status::Internal;
    if (!author_hash || !request.operation.data || request.operation.size != 16 || request.expected_parent.size ||
        !track_sharing::reservationValid(current) || current.revision != 1 || current.generation != request.generation ||
        static_cast<uint8_t>(current.phase) > static_cast<uint8_t>(track_sharing::ReservationPhase::Signed) ||
        std::memcmp(current.author, author_hash, 32) || std::memcmp(current.operation, request.operation.data, 16)) return Status::Conflict;
    if (media != storage::sd_media_session() || !storage::sd_card_ready() || storage::sd_external_block_owner_active()) return Status::StorageUnavailable;
    const auto validated = store.validateCurrent(current, verify, scratch, capacity);
    if (validated != track_sharing::ReservationStoreResult::Ready) return validated == track_sharing::ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
    auto* path = reinterpret_cast<char*>(scratch);
    if (!track_sharing::resolveObjectPath({current.object, 32}, track_sharing::ObjectFile::Directory, {}, path, 160)) return Status::InvalidField;
    storage::SdRuntimeDir directory;
    const auto opened = directory.open_read_status(path, media);
    if (opened == storage::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != storage::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned count = 0; count < 5; ++count)
    {
        bool is_directory = false;
        uint64_t bytes = 0, next = 0;
        auto* name = reinterpret_cast<char*>(scratch + capacity - 128);
        const auto entry = directory.read_next_info(name, 128, &is_directory, &bytes, &next);
        if (entry == storage::SdDirReadStatus::Busy) return Status::Busy;
        if (entry == storage::SdDirReadStatus::End) return media == storage::sd_media_session() && storage::sd_card_ready() && !storage::sd_external_block_owner_active() ? Status::Ok : Status::StorageUnavailable;
        if (entry != storage::SdDirReadStatus::Entry) return Status::StorageUnavailable;
        if (is_directory) return Status::Conflict;
        const size_t length = std::strlen(name), key_size = length == 36 ? 16 : length == 68 ? 32
                                                                                             : 0;
        if (!key_size) return Status::Conflict;
        const auto* key = key_size == 16 ? current.operation : current.candidate;
        for (size_t i = 0; i < key_size; ++i)
            if (name[i * 2] != hex[key[i] >> 4] || name[i * 2 + 1] != hex[key[i] & 15]) return Status::Conflict;
        const auto* suffix = name + key_size * 2;
        if (key_size == 16 ? std::strcmp(suffix, ".rva") && std::strcmp(suffix, ".rvb") : std::strcmp(suffix, ".tmp") && std::strcmp(suffix, ".cmp")) return Status::Conflict;
    }
    return Status::Conflict;
}
} // namespace hostlink::management::prepare_runtime
