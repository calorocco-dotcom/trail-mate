#include "track_sharing/upload_session.h"
#include <cassert>
struct Owner
{
    unsigned cancellations = 0, steps = 0, writes = 0;
    track_sharing::JournalStoreResult write_result = track_sharing::JournalStoreResult::Ready;
    void cancel() { ++cancellations; }
    track_sharing::UploadSealResult step(uint8_t*, size_t)
    {
        ++steps;
        return track_sharing::UploadSealResult::Pending;
    }
    track_sharing::UploadSealResult beginSeal(uint8_t*, size_t) { return track_sharing::UploadSealResult::Pending; }
    track_sharing::JournalStoreResult append(const uint8_t*, uint32_t, const uint8_t*, size_t, uint8_t*, size_t)
    {
        ++writes;
        return write_result;
    }
};
int main()
{
    using namespace track_sharing;
    uint8_t session[16] = {1}, other[16] = {2}, zero[16]{}, scratch[128];
    {
        Owner rejected;
        rejected.write_result = JournalStoreResult::Conflict;
        UploadSession<Owner> lease(rejected, session, 7, 100);
        assert(lease.matches(session, 7, 30099));
        assert(lease.append(session, 7, 30099, session, 0, session, 1, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
        assert(lease.advance(7, 30100, true, scratch, sizeof(scratch)) == UploadSealResult::Cancelled && lease.releasable());
    }
    for (unsigned mode = 0; mode < 4; ++mode)
    {
        Owner owner;
        UploadSession<Owner> task(owner, session, 7, 100);
        static_assert(sizeof(task) <= 48, "Session must not own payloads");
        assert(!task.authorize(other, 7, 101) && !task.close(other));
        assert(task.append(session, 7, 102, session, 0, session, 1, scratch, sizeof(scratch)) == JournalStoreResult::Ready && owner.writes == 1);
        assert(task.beginSeal(session, 7, 103, scratch, sizeof(scratch)) == UploadSealResult::Pending);
        assert(task.advance(7, 104, true, scratch, sizeof(scratch)) == UploadSealResult::Pending && owner.steps == 1);
        const auto result = task.advance(mode == 1 ? 8 : 7, mode == 2 ? 30103 : mode == 3 ? 102
                                                                                          : 105,
                                         mode != 0, scratch, sizeof(scratch));
        assert(result == UploadSealResult::Cancelled && owner.cancellations == 1 && task.releasable());
        assert(!task.authorize(session, 7, 106) && owner.cancellations == 1);
    }
    Owner owner;
    UploadSession<Owner> task(owner, session, 7, 100);
    assert(!task.authorize(other, 7, 30099));
    assert(task.advance(7, 30100, true, scratch, sizeof(scratch)) == UploadSealResult::Cancelled);
    UploadSession<Owner> invalid(owner, zero, 7, 100);
    assert(invalid.releasable() && !invalid.authorize(zero, 7, 100));
    UploadSession<Owner> closed(owner, session, 7, 100);
    assert(closed.close(session) && closed.releasable() && !closed.close(session));
}
