#pragma once
#include "platform/esp/idf_common/hostlink_track_object_admission.h"
#include "track_sharing/package_cache_paths.h"
namespace hostlink::management::prepare_runtime
{
inline Status ensurePackageCacheDirectory(uint32_t media, geocaching::ByteView revision, uint8_t* scratch, size_t capacity)
{
    namespace sd = platform::esp::arduino_common::storage;
    if (!scratch || capacity < 288 || track_sharing::signingSpansOverlap(revision.data, revision.size, scratch, capacity) ||
        !track_sharing::resolvePackageCachePath(revision, track_sharing::ObjectFile::Directory, {}, reinterpret_cast<char*>(scratch), 160)) return Status::InvalidField;
    for (const char* path : {"/trailmate", track_sharing::kPackageCacheDirectory, static_cast<const char*>(reinterpret_cast<char*>(scratch))})
    {
        if (media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
        if (path == reinterpret_cast<char*>(scratch))
        {
            sd::SdRuntimeDir root(sd::SdRuntimeMemoryPolicy::PsramOnly);
            if (!root.has_backing_storage()) return Status::Busy;
            const auto root_status = root.open_read_status(track_sharing::kPackageCacheDirectory, media);
            if (root_status == sd::SdFileReadStatus::Busy) return Status::Busy;
            if (root_status != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
            auto* name = reinterpret_cast<char*>(scratch + capacity - 128);
            unsigned count = 0;
            bool matched = false, ended = false;
            const auto* wanted = std::strrchr(path, '/') + 1;
            for (unsigned scan = 0; scan <= 64; ++scan)
            {
                bool is_dir = false;
                uint64_t length = 0, next = 0;
                const auto row = root.read_next_info(name, 128, &is_dir, &length, &next);
                if (row == sd::SdDirReadStatus::Busy) return Status::Busy;
                if (row == sd::SdDirReadStatus::End)
                {
                    ended = true;
                    break;
                }
                if (row != sd::SdDirReadStatus::Entry) return Status::StorageUnavailable;
                if (++count > 64) return Status::LimitExceeded;
                if (is_dir && !std::strcmp(name, wanted)) matched = true;
            }
            if (!ended || (!matched && count == 64)) return Status::LimitExceeded;
        }
        sd::SdRuntimeDir directory(sd::SdRuntimeMemoryPolicy::PsramOnly);
        if (!directory.has_backing_storage()) return Status::Busy;
        const auto opened = directory.open_read_status(path, media);
        if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
        if (opened == sd::SdFileReadStatus::Ready) continue;
        if (opened != sd::SdFileReadStatus::Missing || !sd::sd_mkdir(path) || media != sd::sd_media_session()) return Status::StorageUnavailable;
    }
    return Status::Ok;
}
inline Status admitPackageCacheFile(uint32_t media, geocaching::ByteView revision, geocaching::ByteView hash, uint32_t bytes,
                                    track_sharing::ObjectFile final_kind, track_sharing::ObjectFile temporary_kind, uint8_t* scratch, size_t capacity)
{
    namespace sd = platform::esp::arduino_common::storage;
    using track_sharing::ObjectFile;
    constexpr uint64_t limit = 64ULL * 1024 * 1024 + 65536;
    constexpr unsigned file_limit = 134;
    const bool resource = final_kind == ObjectFile::AttachmentResource && temporary_kind == ObjectFile::AttachmentResourceTemporary;
    const bool kinds = resource || (final_kind == ObjectFile::SignedManifest && temporary_kind == ObjectFile::SignedTemporary) ||
                       (final_kind == ObjectFile::KmlFile && temporary_kind == ObjectFile::KmlTemporary) || (final_kind == ObjectFile::AttachmentIndex && temporary_kind == ObjectFile::AttachmentIndexTemporary);
    if (!kinds || !scratch || capacity < 448 || !bytes || bytes > 8388608 || !hash.data || hash.size != 32 ||
        track_sharing::signingSpansOverlap(scratch, capacity, revision.data, revision.size) || track_sharing::signingSpansOverlap(scratch, capacity, hash.data, hash.size)) return Status::InvalidField;
    const geocaching::ByteView key = resource ? hash : geocaching::ByteView{};
    if (!track_sharing::resolvePackageCachePath(revision, final_kind, key, reinterpret_cast<char*>(scratch), 160) ||
        !track_sharing::resolvePackageCachePath(revision, temporary_kind, key, reinterpret_cast<char*>(scratch + 160), 160)) return Status::InvalidField;
    // Caller arena supplies canonical names for comparison, no path locals.
    const char* final_name = std::strrchr(reinterpret_cast<char*>(scratch), '/') + 1;
    const char* temporary_name = std::strrchr(reinterpret_cast<char*>(scratch + 160), '/') + 1;
    sd::SdRuntimeDir directory(sd::SdRuntimeMemoryPolicy::PsramOnly);
    if (!directory.has_backing_storage()) return Status::Busy;
    // Terminate the directory prefix in place without touching either name.
    *const_cast<char*>(final_name - 1) = 0;
    const auto opened = directory.open_read_status(reinterpret_cast<char*>(scratch), media);
    if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    uint64_t total = 0, partial = 0;
    unsigned files = 0;
    bool has_final = false, has_temporary = false;
    auto* name = reinterpret_cast<char*>(scratch + capacity - 128);
    for (unsigned scan = 0; scan <= file_limit; ++scan)
    {
        bool is_dir = false;
        uint64_t length = 0, next = 0;
        const auto row = directory.read_next_info(name, 128, &is_dir, &length, &next);
        if (row == sd::SdDirReadStatus::Busy) return Status::Busy;
        if (row == sd::SdDirReadStatus::End)
        {
            if ((!has_final && !has_temporary && files == file_limit) || (has_final ? 0 : bytes - partial) > limit - total) return Status::LimitExceeded;
            return media == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active() ? Status::Ok : Status::StorageUnavailable;
        }
        if (row != sd::SdDirReadStatus::Entry) return Status::StorageUnavailable;
        if (is_dir || ++files > file_limit || length > limit - total) return Status::LimitExceeded;
        total += length;
        if (!std::strcmp(name, final_name)) has_final = true;
        if (!std::strcmp(name, temporary_name))
        {
            if (length > bytes) return Status::Conflict;
            has_temporary = true;
            partial = length;
        }
    }
    return Status::LimitExceeded;
}
} // namespace hostlink::management::prepare_runtime
