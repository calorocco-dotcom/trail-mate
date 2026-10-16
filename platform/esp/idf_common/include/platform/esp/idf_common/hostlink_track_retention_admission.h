#pragma once
#include "platform/esp/idf_common/hostlink_track_object_admission.h"
namespace hostlink::management::prepare_runtime
{
// Bounded per-object history: 32 MiB and 128 files, including unknown files.
// Caller holds storage exclusion through the subsequent copy. No directory list.
inline Status admitRetainedKml(uint32_t media, geocaching::ByteView object, geocaching::ByteView file_hash,
                               uint32_t bytes, uint8_t* scratch, size_t capacity)
{
    constexpr uint64_t byte_limit = 33554432;
    if (!scratch || capacity < 288 || !file_hash.data || file_hash.size != 32 || !bytes || bytes > 8388608) return Status::InvalidField;
    if (track_sharing::signingSpansOverlap(scratch, capacity, file_hash.data, file_hash.size) ||
        track_sharing::signingSpansOverlap(scratch, capacity, object.data, object.size)) return Status::InvalidField;
    if (!track_sharing::resolveObjectPath(object, track_sharing::ObjectFile::Directory, {}, reinterpret_cast<char*>(scratch), 160)) return Status::InvalidField;
    sd::SdRuntimeDir directory;
    const auto opened = directory.open_read_status(reinterpret_cast<char*>(scratch), media);
    if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    uint64_t total = 0, temporary_bytes = 0;
    unsigned files = 0;
    bool has_temporary = false, has_final = false;
    constexpr char hex[] = "0123456789abcdef";
    auto* name = reinterpret_cast<char*>(scratch + capacity - 128);
    for (unsigned scan = 0; scan <= 128; ++scan)
    {
        bool is_directory = false;
        uint64_t length = 0, next = 0;
        const auto entry = directory.read_next_info(name, 128, &is_directory, &length, &next);
        if (entry == sd::SdDirReadStatus::Busy) return Status::Busy;
        if (entry == sd::SdDirReadStatus::End)
        {
            const uint64_t additional = has_final ? 0 : bytes - temporary_bytes;
            if ((!has_final && !has_temporary && files == 128) || additional > byte_limit - total) return Status::LimitExceeded;
            return media == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active() ? Status::Ok : Status::StorageUnavailable;
        }
        if (entry != sd::SdDirReadStatus::Entry) return Status::StorageUnavailable;
        if (is_directory || ++files > 128 || length > byte_limit - total) return Status::LimitExceeded;
        total += length;
        if (std::strlen(name) != 68) continue;
        bool matches = true;
        for (size_t i = 0; i < 32; ++i) matches = matches && name[2 * i] == hex[file_hash.data[i] >> 4] && name[2 * i + 1] == hex[file_hash.data[i] & 15];
        if (!matches) continue;
        if (!std::strcmp(name + 64, ".kml")) has_final = true;
        if (!std::strcmp(name + 64, ".ktm"))
        {
            if (length > bytes) return Status::Conflict;
            has_temporary = true;
            temporary_bytes = length;
        }
    }
    return Status::LimitExceeded;
}
} // namespace hostlink::management::prepare_runtime
