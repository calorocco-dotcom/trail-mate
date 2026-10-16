#include "platform/esp/idf_common/tracker_snapshot_lease.h"
#include <cassert>
using namespace platform::esp::storage;
using namespace hostlink::management;
static uint32_t media = 1;
static bool external = false, recording = false, held = false, ready = true, replace_on_probe = false, replace_on_remove = false;
static unsigned releases = 0, deletes = 0, publishes = 0;
static auto probe_status = platform::esp::arduino_common::storage::SdFileReadStatus::Ready;
namespace platform::ui::tracker
{
bool try_acquire_snapshot_read()
{
    if (held || recording) return false;
    held = true;
    return true;
}
void release_snapshot_read()
{
    assert(held);
    held = false;
    ++releases;
}
} // namespace platform::ui::tracker
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return ready; }
uint32_t sd_media_session() { return media; }
bool sd_external_block_owner_active() { return external; }
SdFileReadResult sd_read_file(const char*, uint8_t*, size_t)
{
    if (replace_on_probe)
    {
        ++media;
        replace_on_probe = false;
    }
    SdFileReadResult result;
    result.status = probe_status;
    return result;
}
bool sd_remove(const char*, uint32_t expected)
{
    if (replace_on_remove)
    {
        ++media;
        replace_on_remove = false;
    }
    if (expected != media || external) return false;
    ++deletes;
    return true;
}
} // namespace platform::esp::arduino_common::storage
class Io : public RecordFileIo
{
  public:
    explicit Io(uint32_t captured) : session(captured) {}
    OpenResult open(const char*, Mode) override { return {}; }
    bool close(Handle) override { return false; }
    bool size(Handle, uint64_t&) override { return false; }
    bool seek(Handle, uint64_t) override { return false; }
    size_t read(Handle, void*, size_t) override { return 0; }
    size_t write(Handle, const void*, size_t) override { return 0; }
    bool sync(Handle) override { return false; }
    bool publish(const char*, const char*) override
    {
        if (session != media || external || fail) return false;
        ++publishes;
        return true;
    }
    uint32_t session;
    bool fail = false;
};
static uint8_t id[16] = {1};
constexpr const char* temporary = "/trailmate/web-tracks/01000000000000000000000000000000.new";
constexpr const char* destination = "/trailmate/web-tracks/01000000000000000000000000000000.tsn";
int main()
{
    using platform::esp::idf_common::TrackerSnapshotLease;
    Io io(media);
    TrackerSnapshotLease lease(io, "/trackers/closed.gpx", temporary, destination, media);
    recording = true;
    assert(lease.acquire(id, 100, 1) == SnapshotStorageResult::Busy && !held);
    recording = false;
    assert(lease.acquire(id, 100, 1) == SnapshotStorageResult::Ok && held && lease.valid());
    TrackSnapshotMetadata metadata;
    std::memcpy(metadata.id, id, 16);
    metadata.bytes = 101;
    metadata.format = 1;
    assert(!lease.commit(metadata) && publishes == 0);
    metadata.bytes = 100;
    io.fail = true;
    assert(!lease.commit(metadata));
    io.fail = false;
    assert(lease.commit(metadata) && publishes == 1);
    assert(lease.discard() && deletes == 0);
    lease.release();
    lease.release();
    assert(!held && releases == 1);
    assert(lease.acquire(id, 100, 1) == SnapshotStorageResult::Ok);
    probe_status = platform::esp::arduino_common::storage::SdFileReadStatus::Busy;
    assert(!lease.discard() && deletes == 0 && held);
    probe_status = platform::esp::arduino_common::storage::SdFileReadStatus::Ready;
    replace_on_remove = true;
    assert(!lease.discard() && deletes == 0);
    assert(lease.discard() && deletes == 0);
    lease.release();
    Io new_io(media);
    TrackerSnapshotLease fresh(new_io, "/trackers/closed.gpx", temporary, destination, media);
    assert(fresh.acquire(id, 100, 1) == SnapshotStorageResult::Ok);
    replace_on_probe = true;
    assert(fresh.discard() && deletes == 0);
    fresh.release();
    TrackerSnapshotLease invalid(new_io, "/trackers/../secret", temporary, destination, media);
    assert(invalid.acquire(id, 100, 1) == SnapshotStorageResult::Failed && !held);
    TrackerSnapshotLease wrong_id(new_io, "/trackers/closed.gpx", "/trailmate/web-tracks/02000000000000000000000000000000.new", destination, media);
    assert(wrong_id.acquire(id, 100, 1) == SnapshotStorageResult::Failed && !held);
    external = true;
    TrackerSnapshotLease owned(new_io, "/trackers/closed.gpx", temporary, destination, media);
    assert(owned.acquire(id, 100, 1) == SnapshotStorageResult::Failed && !held);
    external = false;
    Io final_io(media);
    TrackerSnapshotLease cleanup(final_io, "/trackers/closed.gpx", temporary, destination, media);
    assert(cleanup.acquire(id, 100, 1) == SnapshotStorageResult::Ok);
    assert(cleanup.discard() && deletes == 1);
    probe_status = platform::esp::arduino_common::storage::SdFileReadStatus::Missing;
    assert(cleanup.discard() && deletes == 1);
    cleanup.release();
    return 0;
}
