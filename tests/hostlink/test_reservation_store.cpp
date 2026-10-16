#include "track_sharing/reservation_store.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
struct Io
{
    enum class Mode
    {
        Read,
        CreateTemporary
    };
    enum class OpenStatus
    {
        Ready,
        Missing,
        Unavailable
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    std::string path;
    bool active = false, short_write = false, unavailable = false, corrupt_write = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        if (unavailable) return {nullptr, OpenStatus::Unavailable};
        if (mode == Mode::Read && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        active = false;
        return true;
    }
    bool size(void*, uint64_t& size)
    {
        size = files[path].size();
        return true;
    }
    bool seek(void*, uint64_t position) { return !position; }
    size_t read(void*, uint8_t* out, size_t size)
    {
        size = std::min(size, files[path].size());
        std::memcpy(out, files[path].data(), size);
        return size;
    }
    size_t write(void*, const uint8_t* bytes, size_t size)
    {
        if (short_write) size /= 2;
        files[path].assign(bytes, bytes + size);
        if (corrupt_write && !files[path].empty()) files[path][0] ^= 1;
        return size;
    }
    bool sync(void*) { return true; }
};
int main()
{
    using namespace track_sharing;
    using Result = ReservationStoreResult;
    Io io;
    ReservationStore<Io> store(io, "a", "b");
    static_assert(sizeof(store) <= 24, "Store must borrow records");
    RevisionReservation current, verify, initial;
    uint8_t scratch[168];
    initial.operation[0] = 1;
    initial.object[0] = 2;
    initial.author[0] = 3;
    initial.candidate[0] = 4;
    initial.sequence = 1;
    initial.generation = 7;
    initial.revision = 1;
    assert(store.initialize(initial, verify, scratch, sizeof(scratch)) == Result::Ready);
    current = initial;
    assert(store.advance(current, ReservationPhase::Signed, verify, scratch, sizeof(scratch)) == Result::Conflict);
    io.short_write = true;
    assert(store.advance(current, ReservationPhase::Checked, verify, scratch, sizeof(scratch)) == Result::StorageFailure && !io.active);
    assert(store.load(current, verify, scratch, sizeof(scratch)) == Result::Ready && current.sequence == 1);
    io.short_write = false;
    io.corrupt_write = true;
    assert(store.advance(current, ReservationPhase::Checked, verify, scratch, sizeof(scratch)) == Result::StorageFailure && !io.active);
    assert(store.load(current, verify, scratch, sizeof(scratch)) == Result::Ready && current.sequence == 1);
    io.corrupt_write = false;
    assert(store.advance(current, ReservationPhase::Checked, verify, scratch, sizeof(scratch)) == Result::Ready);
    current = verify;
    assert(store.advance(initial, ReservationPhase::Checked, verify, scratch, sizeof(scratch)) == Result::Conflict);
    const auto stale_checked = current;
    assert(store.advance(current, ReservationPhase::Signed, verify, scratch, sizeof(scratch)) == Result::Ready);
    current = verify;
    const auto signed_slot = io.files["a"];
    assert(store.advance(stale_checked, ReservationPhase::Cancelled, verify, scratch, sizeof(scratch)) == Result::Conflict && io.files["a"] == signed_slot && !io.active);
    const auto saved_files = io.files;
    assert(store.initialize(initial, verify, scratch, sizeof(scratch)) == Result::Conflict && io.files == saved_files);
    assert(store.advance(current, ReservationPhase::Cancelled, verify, scratch, sizeof(scratch)) == Result::Conflict);
    assert(store.load(current, verify, scratch, sizeof(scratch)) == Result::Ready && current.phase == ReservationPhase::Signed);
    io.unavailable = true;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == Result::StorageFailure && !current.sequence && !io.active);
}
