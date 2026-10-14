#pragma once

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/storage/track_snapshot_file_storage.h"
#include "platform/esp/idf_common/tracker_snapshot_runtime.h"

namespace platform::esp::idf_common
{
// Owner lends canonical paths and a RecordFileIo bound to this same media
// session. Paths live through copy/cleanup; no pathname arrays are stored here.
// Snapshot paths are private id-based .new/.tsn names, never browser paths.
class TrackerSnapshotLease final : public platform::esp::storage::SnapshotFileLease
{
  public:
    TrackerSnapshotLease(platform::esp::storage::RecordFileIo& io, const char* source,
                         const char* temporary, const char* destination, uint32_t session)
        : io_(io), source_(source), temporary_(temporary), destination_(destination), session_(session) {}
    ~TrackerSnapshotLease() override { release(); }
    TrackerSnapshotLease(const TrackerSnapshotLease&) = delete;
    TrackerSnapshotLease& operator=(const TrackerSnapshotLease&) = delete;

    hostlink::management::SnapshotStorageResult acquire(const uint8_t id[16], uint32_t bytes, uint8_t format) override
    {
        using Result = hostlink::management::SnapshotStorageResult;
        if (owned_) return Result::Busy;
        uint8_t bits = 0;
        if (id)
            for (unsigned i = 0; i < 16; ++i) bits |= id[i];
        if (!bits || !bytes || bytes > hostlink::management::kTrackSnapshotMaxBytes || format > 3 ||
            !sourcePathValid(source_) || !snapshotPathValid(temporary_, id, ".new") || !snapshotPathValid(destination_, id, ".tsn")) return Result::Failed;
        if (!mediaAvailable()) return Result::Failed;
        if (!platform::ui::tracker::try_acquire_snapshot_read()) return Result::Busy;
        owned_ = true;
        if (!mediaAvailable())
        {
            release();
            return Result::Failed;
        }
        std::memcpy(id_, id, 16);
        bytes_ = bytes;
        format_ = format;
        published_ = false;
        return Result::Ok;
    }
    bool valid() const override { return owned_ && mediaAvailable(); }
    const char* sourcePath() const override { return source_; }
    const char* temporaryPath() const override { return temporary_; }
    bool commit(const hostlink::management::TrackSnapshotMetadata& metadata) override
    {
        if (!valid() || published_ || metadata.bytes != bytes_ || metadata.format != format_ || std::memcmp(metadata.id, id_, 16)) return false;
        if (!io_.publish(temporary_, destination_)) return false;
        published_ = true;
        return true;
    }
    bool discard() override
    {
        namespace sd = platform::esp::arduino_common::storage;
        if (!owned_ || published_) return true;
        // Replacement/USB-owned media cannot be touched. Abandon the old
        // private candidate for owner-controlled cleanup on that media later.
        if (!mediaAvailable()) return true;
        uint8_t probe = 0;
        const auto status = sd::sd_read_file(temporary_, &probe, 1).status;
        if (!mediaAvailable()) return true;
        if (status == sd::SdFileReadStatus::Missing) return true;
        if (status != sd::SdFileReadStatus::Ready && status != sd::SdFileReadStatus::Invalid) return false;
        return sd::sd_remove(temporary_, session_);
    }
    void release() override
    {
        if (owned_)
        {
            platform::ui::tracker::release_snapshot_read();
            owned_ = false;
        }
    }

  private:
    bool mediaAvailable() const
    {
        namespace sd = platform::esp::arduino_common::storage;
        return session_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active();
    }
    static bool sourcePathValid(const char* path)
    {
        if (!path || std::strncmp(path, "/trackers/", 10)) return false;
        size_t length = 0;
        while (length < 128 && path[length]) ++length;
        if (length <= 10 || length == 128 || !std::strcmp(path + 10, ".") || !std::strcmp(path + 10, "..")) return false;
        for (size_t i = 10; i < length; ++i)
            if (static_cast<uint8_t>(path[i]) < 32 || path[i] == '/' || path[i] == '\\' || path[i] == ':') return false;
        return true;
    }
    static bool snapshotPathValid(const char* path, const uint8_t id[16], const char* suffix)
    {
        constexpr const char* prefix = "/trailmate/web-tracks/";
        constexpr const char* hex = "0123456789abcdef";
        if (!path || std::strncmp(path, prefix, std::strlen(prefix))) return false;
        const char* name = path + std::strlen(prefix);
        for (unsigned i = 0; i < 16; ++i)
            if (!name[i * 2] || name[i * 2] != hex[id[i] >> 4] || !name[i * 2 + 1] || name[i * 2 + 1] != hex[id[i] & 15]) return false;
        return std::strcmp(name + 32, suffix) == 0;
    }
    platform::esp::storage::RecordFileIo& io_;
    const char* source_;
    const char* temporary_;
    const char* destination_;
    uint8_t id_[16]{};
    uint32_t session_, bytes_ = 0;
    uint8_t format_ = 0;
    bool owned_ = false, published_ = false;
};
static_assert(sizeof(TrackerSnapshotLease) <= 80, "Snapshot lease must not own path/file buffers");
} // namespace platform::esp::idf_common
