#pragma once

#include "hostlink/management_envelope.h"
#include "hostlink/snapshot_space_budget.h"

#include "hostlink/track_snapshot_selection.h"
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "platform/esp/common/storage/track_snapshot_service.h"
#include "platform/esp/idf_common/tracker_snapshot_lease.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>

namespace hostlink::management
{
namespace snapshot_runtime
{
namespace sd = platform::esp::arduino_common::storage;
class TrackerSnapshotOwner
{
  public:
    TrackerSnapshotOwner(const TrackSnapshotSelection& selection, uint64_t now)
        : media(selection.media_session), last_touch(now), file(sd::SdRuntimeMemoryPolicy::PsramOnly), io(file, media), hash(context),
          lease(io, source, temporary, destination, media), storage(io, hash, lease), builder(storage), task(builder)
    {
        constexpr char prefix[] = "/trackers/";
        std::memcpy(source, prefix, sizeof(prefix) - 1);
        std::memcpy(source + sizeof(prefix) - 1, selection.filename.data(), selection.filename.size());
        source[sizeof(prefix) - 1 + selection.filename.size()] = '\0';
        constexpr char folder[] = "/trailmate/web-tracks/";
        constexpr char hex[] = "0123456789abcdef";
        std::memcpy(temporary, folder, sizeof(folder) - 1);
        size_t position = sizeof(folder) - 1;
        for (const auto byte : selection.id)
        {
            temporary[position++] = hex[byte >> 4];
            temporary[position++] = hex[byte & 15];
        }
        std::memcpy(destination, temporary, position);
        std::memcpy(temporary + position, ".new", 5);
        std::memcpy(destination + position, ".tsn", 5);
    }
    bool sameSelection(const TrackSnapshotSelection& selection) const
    {
        const auto& metadata = task.metadata();
        return media == selection.media_session && metadata.bytes == selection.bytes && metadata.format == selection.format &&
               std::string_view(source + 10) == selection.filename;
    }
    uint32_t media;
    uint64_t last_touch;
    char source[128]{}, temporary[64]{}, destination[64]{};
    mbedtls_sha256_context context;
    sd::SdRuntimeFile file;
    sd::SdBorrowedRecordFileIo io;
    platform::esp::storage::SnapshotStreamSha256 hash;
    platform::esp::idf_common::TrackerSnapshotLease lease;
    platform::esp::storage::TrackSnapshotFileStorage storage;
    TrackSnapshotBuilder builder;
    SnapshotBuildSession task;
};
static_assert(sizeof(TrackerSnapshotOwner) <= 1024, "Snapshot owner must fit bounded transient PSRAM admission");
// All access is serialized by the existing application command scheduler.
inline TrackerSnapshotOwner* owner = nullptr;
inline uint64_t nowMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }
inline void destroyOwner()
{
    if (!owner || !owner->task.releasable()) return;
    owner->~TrackerSnapshotOwner();
    heap_caps_free(owner);
    owner = nullptr;
}
inline void advance(uint8_t* scratch, size_t capacity, bool connected)
{
    if (!owner) return;
    const auto now = nowMs();
    owner->task.advance(scratch, capacity, now, connected);
    if (owner->task.releasable() && (!connected || now < owner->last_touch || now - owner->last_touch >= SnapshotBuildSession::kIdleTimeoutMs)) destroyOwner();
}
inline void closeSession(const uint8_t session[16])
{
    if (!owner || !owner->task.cancel(session, owner->task.metadata().id)) return;
    // Force result retirement after cleanup; a closed management session
    // cannot keep the transient owner admitted while HostLink stays ready.
    owner->last_touch = UINT64_MAX;
}
inline bool retireForStop()
{
    advance(nullptr, 0, false);
    if (!owner) return true;
    if (!owner->task.abandonForStop()) return false;
    destroyOwner();
    return owner == nullptr;
}
inline bool uncommittedSnapshotName(const char* name)
{
    if (!name || std::strlen(name) != 36) return false;
    for (unsigned i = 0; i < 32; ++i)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f') ||
              (name[i] >= 'A' && name[i] <= 'F'))) return false;
    constexpr char suffix[] = ".new";
    for (unsigned i = 0; i < 4; ++i)
    {
        const char value = name[32 + i] >= 'A' && name[32 + i] <= 'Z' ? char(name[32 + i] + 32) : name[32 + i];
        if (value != suffix[i]) return false;
    }
    return true;
}
inline Status admitSpace(uint32_t media, uint32_t bytes, uint8_t* output, size_t capacity)
{
    if (owner) return Status::Busy;
    if (!output || capacity < 384) return Status::Internal;
    sd::SdRuntimeDir directory;
    const auto opened = directory.open_read_status("/trailmate/web-tracks", media);
    if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    SnapshotSpaceBudget budget(bytes);
    for (unsigned scan = 0; scan < 64; ++scan)
    {
        bool is_dir = false;
        uint64_t size = 0, next = 0;
        const auto result = directory.read_next_info(reinterpret_cast<char*>(output + capacity - 128), 128, &is_dir, &size, &next);
        if (result == sd::SdDirReadStatus::Busy) return Status::Busy;
        if (result == sd::SdDirReadStatus::End)
            return media == sd::sd_media_session() && !sd::sd_external_block_owner_active() && budget.complete() ? Status::Ok : Status::StorageUnavailable;
        if (result != sd::SdDirReadStatus::Entry) return Status::StorageUnavailable;
        auto* name = reinterpret_cast<char*>(output + capacity - 128);
        if (!is_dir && uncommittedSnapshotName(name))
        {
            // Admission runs only after the previous owner was retired. Keep
            // the path in the same borrowed tail, away from request views.
            constexpr char prefix[] = "/trailmate/web-tracks/";
            std::memmove(name + sizeof(prefix) - 1, name, 37);
            std::memcpy(name, prefix, sizeof(prefix) - 1);
            if (media != sd::sd_media_session() || sd::sd_external_block_owner_active() || !sd::sd_remove(name, media))
                return Status::StorageUnavailable;
            continue;
        }
        if (!budget.add(size, is_dir)) return Status::LimitExceeded;
    }
    return Status::LimitExceeded;
}
inline Status build(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    TrackSnapshotSelection selection;
    if (!request.session || !decodeTrackSnapshotSelection({request.body, request.body_size}, selection)) return Status::InvalidField;
    const auto now = nowMs();
    if (owner && owner->task.matches(request.session, selection.id))
    {
        if (!owner->sameSelection(selection)) return Status::Conflict;
        owner->last_touch = now;
        owner->task.touch(request.session, selection.id, now);
        return encodeTrackSnapshotBuildState(owner->task, output, capacity, written) ? Status::Ok : Status::Internal;
    }
    if (owner)
    {
        if (!owner->task.releasable()) return Status::Busy;
        destroyOwner();
    }
    if (selection.media_session != sd::sd_media_session()) return Status::Conflict;
    if (!sd::sd_card_ready() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    // Create only private directories; no internal-flash allocation fallback.
    if ((!sd::sd_is_directory("/trailmate") && !sd::sd_mkdir("/trailmate")) ||
        (!sd::sd_is_directory("/trailmate/web-tracks") && !sd::sd_mkdir("/trailmate/web-tracks"))) return Status::StorageUnavailable;
    if (selection.media_session != sd::sd_media_session() || sd::sd_external_block_owner_active()) return Status::Conflict;
    const auto admission = admitSpace(selection.media_session, selection.bytes, output, capacity);
    if (admission != Status::Ok) return admission;
    void* memory = heap_caps_malloc(sizeof(TrackerSnapshotOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return Status::Busy;
    owner = new (memory) TrackerSnapshotOwner(selection, now);
    if (!owner->file.has_backing_storage())
    {
        destroyOwner();
        return Status::Busy;
    }
    // Do not truncate an orphaned candidate or an existing immutable snapshot.
    uint8_t probe = 0;
    const auto temporary_status = sd::sd_read_file(owner->temporary, &probe, 1).status;
    const auto destination_status = sd::sd_read_file(owner->destination, &probe, 1).status;
    if (temporary_status != sd::SdFileReadStatus::Missing || destination_status != sd::SdFileReadStatus::Missing)
    {
        destroyOwner();
        return (temporary_status == sd::SdFileReadStatus::Ready || destination_status == sd::SdFileReadStatus::Ready) ? Status::Conflict : Status::StorageUnavailable;
    }
    const auto started = owner->task.begin(request.session, selection.id, selection.bytes, selection.format, now);
    if (started != SnapshotBuildResult::Pending)
    {
        destroyOwner();
        return started == SnapshotBuildResult::Busy ? Status::Busy : Status::InvalidField;
    }
    return encodeTrackSnapshotBuildState(owner->task, output, capacity, written) ? Status::Ok : Status::Internal;
}
inline Status releaseTrackSnapshot(const EnvelopeView& request, platform::esp::storage::RecordFileIo& io,
                                   uint32_t media, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    geocaching::ByteView raw;
    if (!request.session || !reader.array(fields, 1) || fields != 1 || !reader.binary(raw, 16) || raw.size != 16 || !reader.finished()) return Status::InvalidField;
    uint8_t id[16];
    std::memcpy(id, raw.data, 16);
    if (owner && !owner->task.releasable()) return Status::Busy;
    if (owner && !std::memcmp(owner->task.metadata().id, id, 16) && !owner->task.matches(request.session, id)) return Status::Unauthorized;
    if (!output || capacity < 464) return Status::Internal;
    // Reuse operation 81 validation to check the exact device-owned container.
    // Its path remains in output's last 64 bytes after the one-byte read.
    geocaching::protocol::CmpWriter writer(output, capacity - 64);
    if (!writer.array(4) || !writer.binary({id, 16}) || !writer.unsignedInteger(0) || !writer.unsignedInteger(1) || !writer.nil()) return Status::Internal;
    EnvelopeView read;
    read.body = output;
    read.body_size = writer.size();
    size_t ignored = 0;
    const auto status = readTrackSnapshotService(read, io, output, capacity, ignored);
    if (status != Status::Ok && status != Status::Expired) return status;
    if (media != sd::sd_media_session() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    if (status == Status::Ok && !sd::sd_remove(reinterpret_cast<char*>(output + capacity - 64), media)) return Status::StorageUnavailable;
    if (owner && !std::memcmp(owner->task.metadata().id, id, 16)) destroyOwner();
    output[0] = 0x90;
    written = 1;
    return Status::Ok;
}
inline Status cancel(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    geocaching::ByteView id;
    if (!request.session || !reader.array(fields, 1) || fields != 1 || !reader.binary(id, 16) || id.size != 16 || !reader.finished()) return Status::InvalidField;
    if (!owner) return Status::Expired;
    if (!owner->task.cancel(request.session, id.data)) return Status::Unauthorized;
    return encodeTrackSnapshotBuildState(owner->task, output, capacity, written) ? Status::Ok : Status::Internal;
}
} // namespace snapshot_runtime
} // namespace hostlink::management
