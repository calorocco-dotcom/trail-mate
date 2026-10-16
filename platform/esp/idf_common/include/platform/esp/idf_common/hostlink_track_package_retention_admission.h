#pragma once
#include "platform/esp/idf_common/hostlink_track_object_admission.h"
namespace hostlink::management::prepare_runtime
{
// v2 object history budget: 128MiB + bounded metadata room, <=512 files.
// Counts unknown/orphan files. Source leases remain held through copying.
inline Status admitRetainedPackageFile(uint32_t media, geocaching::ByteView object, geocaching::ByteView hash, uint32_t bytes,
                                       track_sharing::ObjectFile final_kind, track_sharing::ObjectFile temporary_kind, uint8_t* scratch, size_t capacity)
{
    using track_sharing::ObjectFile;
    const bool kinds = (final_kind == ObjectFile::KmlFile && temporary_kind == ObjectFile::KmlTemporary) ||
                       (final_kind == ObjectFile::AttachmentIndex && temporary_kind == ObjectFile::AttachmentIndexTemporary) ||
                       (final_kind == ObjectFile::AttachmentResource && temporary_kind == ObjectFile::AttachmentResourceTemporary);
    constexpr uint64_t limit = 128ULL * 1024 * 1024 + 1024 * 1024;
    if (!kinds || !bytes || bytes > 8388608 || !scratch || capacity < 448 || !hash.data || hash.size != 32 ||
        track_sharing::signingSpansOverlap(scratch, capacity, hash.data, hash.size) ||
        track_sharing::signingSpansOverlap(scratch, capacity, object.data, object.size)) return Status::InvalidField;
    // Two paths are borrowed only until directory enumeration starts.
    if (!track_sharing::resolveObjectPath(object, final_kind, hash, reinterpret_cast<char*>(scratch), 160) ||
        !track_sharing::resolveObjectPath(object, temporary_kind, hash, reinterpret_cast<char*>(scratch + 160), 160)) return Status::InvalidField;
    // Only suffixes are needed during the scan; no path arrays on the task stack.
    const auto* suffix = reinterpret_cast<const char*>(scratch + 160 + std::strlen(reinterpret_cast<const char*>(scratch + 160)) - 4);
    uint32_t temporary_suffix = 0;
    std::memcpy(&temporary_suffix, suffix, 4);
    suffix = reinterpret_cast<const char*>(scratch + std::strlen(reinterpret_cast<const char*>(scratch)) - 4);
    uint32_t final_suffix = 0;
    std::memcpy(&final_suffix, suffix, 4);
    if (!track_sharing::resolveObjectPath(object, ObjectFile::Directory, {}, reinterpret_cast<char*>(scratch), 160)) return Status::InvalidField;
    sd::SdRuntimeDir directory(sd::SdRuntimeMemoryPolicy::PsramOnly);
    if (!directory.has_backing_storage()) return Status::Busy;
    const auto opened = directory.open_read_status(reinterpret_cast<char*>(scratch), media);
    if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    uint64_t total = 0, partial = 0;
    unsigned files = 0;
    bool has_final = false, has_temporary = false;
    constexpr char hex[] = "0123456789abcdef";
    auto* name = reinterpret_cast<char*>(scratch + capacity - 128);
    for (unsigned scan = 0; scan <= 512; ++scan)
    {
        bool is_directory = false;
        uint64_t length = 0, next = 0;
        const auto row = directory.read_next_info(name, 128, &is_directory, &length, &next);
        if (row == sd::SdDirReadStatus::Busy) return Status::Busy;
        if (row == sd::SdDirReadStatus::End)
        {
            const uint64_t additional = has_final ? 0 : bytes - partial;
            if ((!has_final && !has_temporary && files == 512) || additional > limit - total) return Status::LimitExceeded;
            return media == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active() ? Status::Ok : Status::StorageUnavailable;
        }
        if (row != sd::SdDirReadStatus::Entry) return Status::StorageUnavailable;
        if (is_directory || ++files > 512 || length > limit - total) return Status::LimitExceeded;
        total += length;
        if (std::strlen(name) != 68) continue;
        bool matches = true;
        for (size_t i = 0; i < 32; ++i) matches = matches && name[2 * i] == hex[hash.data[i] >> 4] && name[2 * i + 1] == hex[hash.data[i] & 15];
        if (!matches) continue;
        uint32_t actual_suffix = 0;
        std::memcpy(&actual_suffix, name + 64, 4);
        if (actual_suffix == final_suffix) has_final = true;
        if (actual_suffix == temporary_suffix)
        {
            if (length > bytes) return Status::Conflict;
            has_temporary = true;
            partial = length;
        }
    }
    return Status::LimitExceeded;
}
} // namespace hostlink::management::prepare_runtime
