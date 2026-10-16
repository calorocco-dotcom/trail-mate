#include "hostlink/track_snapshot_builder.h"
#include <cassert>
#include <vector>

using namespace hostlink::management;
class Storage : public TrackSnapshotStorage
{
  public:
    SnapshotStorageResult begin(const uint8_t id[16], uint32_t bytes, uint8_t format) override
    {
        std::memcpy(metadata.id, id, 16);
        metadata.bytes = bytes;
        metadata.format = format;
        lease = true;
        offset = 0;
        return begin_fail ? SnapshotStorageResult::Failed : SnapshotStorageResult::Ok;
    }
    SnapshotStorageResult copy(uint32_t position, uint8_t* scratch, size_t length) override
    {
        assert(lease && position == offset && length <= 320);
        requests.push_back(position);
        if (busy)
        {
            busy = false;
            return SnapshotStorageResult::Busy;
        }
        if (fail) return SnapshotStorageResult::Failed;
        std::memset(scratch, 0x55, length);
        offset += length;
        return SnapshotStorageResult::Ok;
    }
    SnapshotStorageResult finish(TrackSnapshotMetadata& output) override
    {
        assert(lease && offset == metadata.bytes);
        if (busy)
        {
            busy = false;
            return SnapshotStorageResult::Busy;
        }
        output = metadata;
        output.sealed = !bad_seal;
        if (!bad_seal) lease = false;
        return SnapshotStorageResult::Ok;
    }
    SnapshotStorageResult abort() override
    {
        ++aborts;
        if (abort_fail) return SnapshotStorageResult::Failed;
        if (busy)
        {
            busy = false;
            return SnapshotStorageResult::Busy;
        }
        lease = false;
        return SnapshotStorageResult::Ok;
    }
    TrackSnapshotMetadata metadata;
    bool lease = false, busy = false, fail = false, bad_seal = false, begin_fail = false, abort_fail = false;
    size_t offset = 0, aborts = 0;
    std::vector<uint32_t> requests;
};
static uint8_t scratch[320], id[16] = {1};
int main()
{
    Storage storage;
    TrackSnapshotBuilder builder(storage);
    TrackSnapshotMetadata output;
    assert(builder.begin(id, 700, 2) == SnapshotBuildResult::Pending);
    assert(builder.begin(id, 700, 2) == SnapshotBuildResult::Busy);
    storage.busy = true;
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Pending && builder.copiedBytes() == 0);
    for (unsigned i = 0; i < 3; ++i) assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Pending && !output.sealed);
    assert(builder.copiedBytes() == 700 && storage.lease);
    storage.busy = true;
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Pending && !output.sealed);
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Ready && output.sealed && !storage.lease);
    assert((storage.requests == std::vector<uint32_t>{0, 0, 320, 640}));
    assert(builder.begin(id, 10, 2) == SnapshotBuildResult::Pending);
    assert(builder.step(nullptr, 320, output) == SnapshotBuildResult::Invalid && builder.copiedBytes() == 0);
    assert(builder.cancel() == SnapshotBuildResult::Pending);
    storage.busy = true;
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Pending && storage.lease);
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Cancelled && !storage.lease);
    assert(builder.begin(id, 10, 2) == SnapshotBuildResult::Pending);
    storage.fail = true;
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Pending && builder.copiedBytes() == 0);
    assert(builder.begin(id, 10, 2) == SnapshotBuildResult::Busy);
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Failed && !storage.lease);
    storage.fail = false;
    storage.bad_seal = true;
    assert(builder.begin(id, 1, 2) == SnapshotBuildResult::Pending);
    assert(builder.step(scratch, 1, output) == SnapshotBuildResult::Pending);
    assert(builder.step(scratch, 1, output) == SnapshotBuildResult::Pending && !output.sealed);
    assert(builder.step(scratch, 1, output) == SnapshotBuildResult::Failed && !storage.lease);
    assert(builder.begin(id, kTrackSnapshotMaxBytes + 1, 2) == SnapshotBuildResult::Invalid);
    storage.begin_fail = true;
    assert(builder.begin(id, 10, 2) == SnapshotBuildResult::Pending && storage.lease);
    assert(builder.begin(id, 10, 2) == SnapshotBuildResult::Busy);
    storage.busy = true;
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Pending && storage.lease);
    storage.abort_fail = true;
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Failed && storage.lease);
    assert(builder.begin(id, 10, 2) == SnapshotBuildResult::Busy);
    storage.abort_fail = false;
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Failed && !storage.lease);
    storage.begin_fail = false;
    storage.bad_seal = false;
    assert(builder.begin(id, 10, 2) == SnapshotBuildResult::Pending);
    assert(builder.cancel() == SnapshotBuildResult::Pending);
    assert(builder.step(scratch, sizeof(scratch), output) == SnapshotBuildResult::Cancelled && !storage.lease);
    return 0;
}
