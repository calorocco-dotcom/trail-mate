#include "hostlink/track_snapshot_session.h"
#include <cassert>
using namespace hostlink::management;
struct Storage final : TrackSnapshotStorage
{
    TrackSnapshotMetadata metadata;
    unsigned copies = 0, aborts = 0;
    bool owned = false, fail_begin = false, fail_copy = false, fail_abort = false, busy_abort = false;
    SnapshotStorageResult begin(const uint8_t id[16], uint32_t bytes, uint8_t format) override
    {
        owned = true;
        std::memcpy(metadata.id, id, 16);
        metadata.bytes = bytes;
        metadata.format = format;
        return fail_begin ? SnapshotStorageResult::Failed : SnapshotStorageResult::Ok;
    }
    SnapshotStorageResult copy(uint32_t, uint8_t*, size_t) override
    {
        ++copies;
        return fail_copy ? SnapshotStorageResult::Failed : SnapshotStorageResult::Ok;
    }
    SnapshotStorageResult finish(TrackSnapshotMetadata& output) override
    {
        metadata.sealed = true;
        output = metadata;
        owned = false;
        return SnapshotStorageResult::Ok;
    }
    SnapshotStorageResult abort() override
    {
        ++aborts;
        if (busy_abort) return SnapshotStorageResult::Busy;
        if (fail_abort) return SnapshotStorageResult::Failed;
        owned = false;
        return SnapshotStorageResult::Ok;
    }
};
int main()
{
    uint8_t session[16] = {1}, other[16] = {2}, id[16] = {3}, scratch[320];
    {
        Storage storage;
        TrackSnapshotBuilder builder(storage);
        SnapshotBuildSession task(builder);
        assert(task.begin(session, id, 750, 1, 100) == SnapshotBuildResult::Pending);
        assert(!task.releasable() && !task.touch(other, id, 101) && !task.cancel(other, id));
        assert(task.begin(session, id, 750, 1, 101) == SnapshotBuildResult::Busy);
        for (unsigned i = 0; i < 3; ++i) task.advance(scratch, sizeof(scratch), 102 + i, true);
        assert(task.copiedBytes() == 750 && task.state() == SnapshotSessionState::Building);
        task.advance(scratch, sizeof(scratch), 106, true);
        assert(task.state() == SnapshotSessionState::Ready && task.metadata().sealed && task.releasable());
        assert(task.cancel(session, id));
        task.advance(scratch, sizeof(scratch), 50000, false);
        assert(task.state() == SnapshotSessionState::Ready && storage.aborts == 0);
    }
    {
        Storage storage;
        storage.fail_abort = true;
        TrackSnapshotBuilder builder(storage);
        SnapshotBuildSession task(builder);
        assert(task.begin(session, id, 750, 1, 100) == SnapshotBuildResult::Pending);
        task.advance(scratch, sizeof(scratch), 101, false);
        assert(task.state() == SnapshotSessionState::Cancelling && !task.releasable() && storage.owned);
        assert(!task.abandonForStop() && !task.releasable() && storage.owned);
        storage.fail_abort = false;
        task.advance(scratch, sizeof(scratch), 102, false);
        assert(task.state() == SnapshotSessionState::Cancelled && task.releasable() && !storage.owned);
    }
    {
        Storage storage;
        storage.busy_abort = true;
        TrackSnapshotBuilder builder(storage);
        SnapshotBuildSession task(builder);
        assert(task.begin(session, id, 750, 1, 0) == SnapshotBuildResult::Pending);
        assert(task.touch(session, id, 1000));
        task.advance(scratch, sizeof(scratch), 30000, true);
        assert(task.state() == SnapshotSessionState::Building && storage.aborts == 0);
        task.advance(scratch, sizeof(scratch), 31000, true);
        assert(task.state() == SnapshotSessionState::Cancelling && !task.releasable());
        storage.busy_abort = false;
        task.advance(scratch, sizeof(scratch), 31001, true);
        assert(task.state() == SnapshotSessionState::Cancelled && task.releasable());
    }
    for (bool initialization : {false, true})
    {
        Storage storage;
        storage.fail_begin = initialization;
        storage.fail_copy = !initialization;
        storage.fail_abort = true;
        TrackSnapshotBuilder builder(storage);
        SnapshotBuildSession task(builder);
        assert(task.begin(session, id, 750, 1, 0) == SnapshotBuildResult::Pending);
        task.advance(scratch, sizeof(scratch), 1, true);
        task.advance(scratch, sizeof(scratch), 2, true);
        assert(!task.releasable() && storage.owned);
        storage.fail_abort = false;
        task.advance(scratch, sizeof(scratch), 3, true);
        assert(task.state() == SnapshotSessionState::Failed && task.releasable() && !storage.owned);
    }
}
