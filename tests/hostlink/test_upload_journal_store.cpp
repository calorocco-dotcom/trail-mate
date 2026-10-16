#include "track_sharing/upload_journal_store.h"
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
    size_t offset = 0;
    bool active = false, short_write = false, unavailable_b = false, corrupt_write = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        offset = 0;
        if (unavailable_b && path == "b") return {nullptr, OpenStatus::Unavailable};
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
    bool size(void*, uint64_t& value)
    {
        value = files[path].size();
        return true;
    }
    bool seek(void*, uint64_t position)
    {
        offset = size_t(position);
        return offset <= files[path].size();
    }
    size_t read(void*, uint8_t* output, size_t length)
    {
        length = std::min(length, files[path].size() - offset);
        std::memcpy(output, files[path].data() + offset, length);
        offset += length;
        return length;
    }
    size_t write(void*, const uint8_t* input, size_t length)
    {
        if (short_write) length /= 2;
        files[path].assign(input, input + length);
        if (corrupt_write && !files[path].empty()) files[path][0] ^= 1;
        return length;
    }
    bool sync(void*) { return true; }
};
int main()
{
    using namespace track_sharing;
    Io io;
    UploadJournalStore<Io> store(io, "a", "b");
    static_assert(sizeof(store) <= 24, "Journal store grew permanent metadata");
    uint8_t scratch[108];
    UploadJournal current, next, verify, loaded;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == JournalStoreResult::Missing);
    next.id[0] = 1;
    next.author_hash[0] = 2;
    next.file_hash[0] = 3;
    next.generation = 7;
    next.total = 1000;
    next.sequence = 1;
    {
        Io fresh;
        UploadJournalStore<Io> first(fresh, "a", "b");
        assert(first.initialize(next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && verify.sequence == 1 && !fresh.active);
        const auto saved = fresh.files["a"];
        assert(first.initialize(next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict && fresh.files["a"] == saved);
        fresh.files.clear();
        fresh.files["b"] = {1, 2, 3};
        assert(first.initialize(next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict && !fresh.files.count("a"));
        fresh.files.clear();
        fresh.unavailable_b = true;
        assert(first.initialize(next, verify, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure && !fresh.files.count("a"));
        fresh.unavailable_b = false;
        fresh.short_write = true;
        assert(first.initialize(next, verify, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure && !fresh.active);
        fresh.short_write = false;
        assert(first.initialize(next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
        assert(first.initialize(next, next, scratch, sizeof(scratch)) == JournalStoreResult::Invalid);
    }
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && !io.active);
    current = next;
    next.sequence = 2;
    next.durable_offset = 320;
    assert(store.advance(current, 1001, UploadPhase::Receiving, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    assert(store.advance(current, 320, UploadPhase::Checking, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    assert(store.advance(current, 0, UploadPhase::Receiving, current, scratch, sizeof(scratch)) == JournalStoreResult::Invalid);
    io.short_write = true;
    assert(store.advance(current, 320, UploadPhase::Receiving, verify, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure && current.sequence == 1 && !io.active);
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure && !io.active);
    assert(store.load(loaded, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && loaded.sequence == 1);
    io.short_write = false;
    io.corrupt_write = true;
    assert(store.advance(current, 320, UploadPhase::Receiving, verify, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure && current.durable_offset == 0);
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure);
    assert(store.load(loaded, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && loaded.sequence == 1);
    io.corrupt_write = false;
    assert(store.advance(current, 320, UploadPhase::Receiving, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
    assert(verify.sequence == 2 && verify.durable_offset == 320 && current.sequence == 1);
    const auto newer_slot = io.files["b"];
    assert(store.commit(current, next, loaded, scratch, sizeof(scratch)) == JournalStoreResult::Conflict && io.files["b"] == newer_slot && !io.active);
    assert(store.advance(current, 320, UploadPhase::Receiving, loaded, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    current = next;
    next.sequence = 3;
    next.durable_offset = 1000;
    next.phase = UploadPhase::Sealed;
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    next.phase = UploadPhase::Checking;
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
    current = next;
    next.sequence = 4;
    next.phase = UploadPhase::Sealed;
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
    current = next;
    assert(store.load(loaded, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && loaded.sequence == 4 && loaded.phase == UploadPhase::Sealed);
    next.sequence = 5;
    next.author_hash[0] ^= 1;
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    next.author_hash[0] ^= 1;
    io.files["b"][5] ^= 1;
    assert(store.load(loaded, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && loaded.sequence == 3);
    assert(store.commit(current, next, verify, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    io.unavailable_b = true;
    assert(store.load(loaded, verify, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure && loaded.sequence == 0 && !io.active);
}
