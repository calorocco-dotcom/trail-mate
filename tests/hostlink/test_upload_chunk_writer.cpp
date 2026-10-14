#include "track_sharing/upload_chunk_writer.h"
#include "track_sharing/upload_seal_task.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
struct ChunkIo
{
    enum class Mode
    {
        Read,
        Update,
        CreateTemporary
    };
    enum class OpenStatus
    {
        Ready,
        Missing
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<std::string> events;
    std::string path;
    size_t offset = 0;
    bool active = false, short_log = false, fail_data_sync = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        offset = 0;
        if (mode != Mode::CreateTemporary && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        events.push_back("open:" + path);
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        assert(active);
        active = false;
        events.push_back("close:" + path);
        return true;
    }
    bool size(void*, uint64_t& value)
    {
        value = files[path].size();
        return true;
    }
    bool seek(void*, uint64_t value)
    {
        offset = size_t(value);
        return offset <= files[path].size();
    }
    size_t read(void*, uint8_t* out, size_t count)
    {
        count = std::min(count, files[path].size() - offset);
        std::memcpy(out, files[path].data() + offset, count);
        offset += count;
        return count;
    }
    size_t write(void*, const uint8_t* in, size_t count)
    {
        if (short_log && path != "data") count /= 2;
        files[path].resize(std::max(files[path].size(), offset + count));
        std::memcpy(files[path].data() + offset, in, count);
        offset += count;
        return count;
    }
    bool sync(void*)
    {
        events.push_back("sync:" + path);
        return !(fail_data_sync && path == "data");
    }
};
struct SealCheck
{
    track_sharing::FileIntegrityResult outcome = track_sharing::FileIntegrityResult::Ready;
    bool cancelled = false;
    unsigned steps = 0;
    track_sharing::FileIntegrityResult begin(const char*, uint32_t total, const uint8_t*)
    {
        assert(total == 640);
        return track_sharing::FileIntegrityResult::Pending;
    }
    track_sharing::FileIntegrityResult step(uint8_t*, size_t) { return ++steps == 1 ? track_sharing::FileIntegrityResult::Pending : outcome; }
    void cancel() { cancelled = true; }
};
int main()
{
    using namespace track_sharing;
    ChunkIo io;
    UploadJournalStore<ChunkIo> store(io, "a", "b");
    UploadJournal current, verify, initial;
    initial.id[0] = 1;
    initial.author_hash[0] = 2;
    initial.generation = 1;
    initial.total = 640;
    initial.sequence = 1;
    uint8_t scratch[128], bytes[320];
    std::memset(bytes, 42, sizeof(bytes));
    assert(store.initialize(initial, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
    current = initial;
    io.files["data"] = {};
    UploadChunkWriter<ChunkIo, decltype(store)> writer(io, store, current, verify, "data", initial.generation, initial.author_hash);
    static_assert(sizeof(writer) <= 56, "Chunk writer must borrow all storage");
    io.events.clear();
    initial.generation = 2;
    assert(writer.append(initial.id, 0, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Conflict && io.events.empty());
    initial.generation = 1;
    initial.author_hash[31] = 1;
    assert(writer.append(initial.id, 0, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Conflict && io.events.empty());
    initial.author_hash[31] = 0;
    io.events.clear();
    io.short_log = true;
    assert(writer.append(initial.id, 0, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure);
    assert(current.durable_offset == 0 && io.files["data"].size() == 320 && !io.active);
    const auto synced = std::find(io.events.begin(), io.events.end(), "sync:data");
    assert(synced != io.events.end() && *(synced + 1) == "close:data" && *(synced + 2) == "open:a");
    assert(store.load(current, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && current.sequence == 1);
    io.short_log = false;
    assert(writer.append(initial.id, 0, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
    assert(current.sequence == 2 && current.durable_offset == 320);
    current = initial;
    bytes[0] = 99;
    io.events.clear();
    assert(writer.append(initial.id, 0, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    assert(io.files["data"][0] == 42 && !io.active);
    assert(std::find(io.events.begin(), io.events.end(), "open:data") == io.events.end());
    bytes[0] = 42;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready && current.sequence == 2);
    assert(writer.append(initial.id, 0, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Ready && current.sequence == 2);
    bytes[200] ^= 1;
    assert(writer.append(initial.id, 0, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Conflict && !io.active);
    bytes[200] ^= 1;
    assert(writer.append(initial.id, 321, bytes, 1, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    initial.id[0] = 9;
    assert(writer.append(initial.id, 320, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
    initial.id[0] = 1;
    io.fail_data_sync = true;
    assert(writer.append(initial.id, 320, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::StorageFailure && current.sequence == 2 && !io.active);
    io.fail_data_sync = false;
    assert(writer.append(initial.id, 320, bytes, 320, scratch, sizeof(scratch)) == JournalStoreResult::Ready && current.durable_offset == 640);
    assert(writer.append(initial.id, 0, scratch, 1, scratch, sizeof(scratch)) == JournalStoreResult::Invalid);
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        ChunkIo sealing_io = io;
        UploadJournal state = current, checked;
        UploadJournalStore<ChunkIo> sealing_store(sealing_io, "a", "b");
        SealCheck check;
        if (mode == 1) check.outcome = FileIntegrityResult::HashMismatch;
        uint32_t generation = 1;
        UploadSealTask<SealCheck, decltype(sealing_store)> task(check, sealing_store, state, checked, generation, initial.author_hash);
        static_assert(sizeof(task) <= 56, "Seal controller must borrow storage");
        assert(task.begin("data", scratch, sizeof(scratch)) == UploadSealResult::Pending && state.phase == UploadPhase::Checking);
        assert(task.step(scratch, sizeof(scratch)) == UploadSealResult::Pending);
        if (mode == 2)
        {
            generation = 2;
            assert(task.step(scratch, sizeof(scratch)) == UploadSealResult::Conflict && check.cancelled && state.phase == UploadPhase::Checking);
        }
        else
        {
            assert(task.step(scratch, sizeof(scratch)) == (mode ? UploadSealResult::HashMismatch : UploadSealResult::Ready));
            assert(state.phase == (mode ? UploadPhase::Failed : UploadPhase::Sealed));
            assert(sealing_store.load(state, checked, scratch, sizeof(scratch)) == JournalStoreResult::Ready && state.phase == (mode ? UploadPhase::Failed : UploadPhase::Sealed));
        }
        assert(!sealing_io.active);
    }
}
