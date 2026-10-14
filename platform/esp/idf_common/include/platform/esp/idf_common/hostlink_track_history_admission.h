#pragma once
#include "hostlink/management_envelope.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "track_sharing/ip_confirmation_request.h"
#include "track_sharing/object_head.h"
#include "track_sharing/object_paths.h"
namespace hostlink::management::ip_confirmation_runtime
{
// Authenticated evidence has already been checked by work. For head absence,
// only this exact import's immutable/temporary evidence may exist. Unknown
// files and unfinished signing block adoption, never get removed or ignored.
inline Status admitImportedObjectDirectory(uint32_t media, const track_sharing::IpConfirmationRequest& request,
                                           uint8_t* scratch, size_t capacity)
{
    namespace storage = platform::esp::arduino_common::storage;
    using namespace track_sharing;
    if (!scratch || capacity < 160 + kObjectHeadBytes) return Status::InvalidField;
    const auto ready = [&]()
    { return media == storage::sd_media_session() && storage::sd_card_ready() && !storage::sd_external_block_owner_active(); };
    if (!ready()) return Status::StorageUnavailable;
    auto* path = reinterpret_cast<char*>(scratch);
    for (const auto slot : {ObjectFile::HeadA, ObjectFile::HeadB})
    {
        if (!resolveObjectPath(request.object, slot, {}, path, 160)) return Status::InvalidField;
        // sd_read_file reads the entire file and rejects a smaller capacity.
        // Borrow the existing command workspace for the fixed 136-byte head;
        // an existence probe of one byte rejects every legitimate saved head.
        const auto read = storage::sd_read_file(path, scratch + 160, kObjectHeadBytes);
        if (!ready()) return Status::StorageUnavailable;
        if (read.status == storage::SdFileReadStatus::Ready) return Status::Ok; // Work validates both slots before mutation.
        if (read.status != storage::SdFileReadStatus::Missing) return Status::StorageUnavailable;
    }
    if (!resolveObjectPath(request.object, ObjectFile::Directory, {}, path, 160)) return Status::InvalidField;
    {
        storage::SdRuntimeDir directory(storage::SdRuntimeMemoryPolicy::PsramOnly);
        if (!directory.has_backing_storage()) return Status::Busy;
        const auto opened = directory.open_read_status(path, media);
        if (opened == storage::SdFileReadStatus::Busy) return Status::Busy;
        if (opened != storage::SdFileReadStatus::Missing && opened != storage::SdFileReadStatus::Ready) return Status::StorageUnavailable;
        if (opened == storage::SdFileReadStatus::Ready)
        {
            constexpr char hex[] = "0123456789abcdef";
            bool ended = false;
            for (unsigned count = 0; count < 5; ++count)
            {
                bool is_directory = false;
                uint64_t bytes = 0, next = 0;
                auto* name = reinterpret_cast<char*>(scratch + 160);
                const auto entry = directory.read_next_info(name, capacity - 160, &is_directory, &bytes, &next);
                if (entry == storage::SdDirReadStatus::End)
                {
                    ended = true;
                    break;
                }
                if (entry == storage::SdDirReadStatus::Busy) return Status::Busy;
                if (entry != storage::SdDirReadStatus::Entry) return Status::StorageUnavailable;
                if (is_directory) return Status::Conflict;
                const size_t length = std::strlen(name);
                const auto key = length == 68 ? request.revision_hash : length == 36 ? request.network_request
                                                                                     : geocaching::ByteView{};
                if (!key.size) return Status::Conflict;
                for (size_t i = 0; i < key.size; ++i)
                    if (name[i * 2] != hex[key.data[i] >> 4] || name[i * 2 + 1] != hex[key.data[i] & 15]) return Status::Conflict;
                const auto* suffix = name + key.size * 2;
                if (key.size == 32 ? std::strcmp(suffix, ".cmp") && std::strcmp(suffix, ".tmp") : std::strcmp(suffix, ".rcp") && std::strcmp(suffix, ".rct")) return Status::Conflict;
            }
            if (!ended) return Status::Conflict;
        }
    }
    // Directory driver storage above is released before creating paths.
    for (const auto* item : {"/trailmate", kObjectDirectory, static_cast<const char*>(path)})
    {
        storage::SdRuntimeDir directory(storage::SdRuntimeMemoryPolicy::PsramOnly);
        if (!directory.has_backing_storage()) return Status::Busy;
        const auto status = directory.open_read_status(item, media);
        if (status == storage::SdFileReadStatus::Busy) return Status::Busy;
        if (status == storage::SdFileReadStatus::Ready) continue;
        if (status != storage::SdFileReadStatus::Missing || !ready() || !storage::sd_mkdir(item) || !ready()) return Status::StorageUnavailable;
    }
    return ready() ? Status::Ok : Status::StorageUnavailable;
}
} // namespace hostlink::management::ip_confirmation_runtime
