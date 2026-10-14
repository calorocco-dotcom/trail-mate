#include "track_sharing/object_head_store.h"
#include "track_sharing/publication_journal_store.h"
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
    bool active = false, short_write = false, unavailable = false;
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
    bool size(void*, uint64_t& value)
    {
        value = files[path].size();
        return true;
    }
    size_t read(void*, uint8_t* out, size_t count)
    {
        count = std::min(count, files[path].size());
        std::memcpy(out, files[path].data(), count);
        return count;
    }
    size_t write(void*, const uint8_t* in, size_t count)
    {
        if (short_write) count /= 2;
        files[path].assign(in, in + count);
        return count;
    }
    bool sync(void*) { return true; }
    bool close(void*)
    {
        active = false;
        return true;
    }
};
int main()
{
    using namespace track_sharing;
    Io io;
    ObjectHeadStore<Io> store(io, "a", "b");
    static_assert(sizeof(store) <= 24, "Head store must borrow all records");
    ObjectHead current, next, verify;
    uint8_t scratch[kObjectHeadBytes];
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Missing);
    current.object[0] = 1;
    current.hash[0] = 2;
    current.operation[0] = 3;
    current.sequence = current.revision = 1;
    current.generation = 7;
    assert(store.initialize(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Ready);
    const auto initial_files = io.files;
    assert(store.initialize(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Conflict && io.files == initial_files);
    next = current;
    next.sequence = 2;
    next.phase = HeadPhase::Committed;
    assert(store.advance(current, next, verify, scratch, sizeof(scratch)) == HeadStoreResult::Ready);
    const auto committed_files = io.files;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Ready);
    assert(store.rebindCommittedGeneration(current, verify, 8, scratch, sizeof(scratch)) == HeadStoreResult::Ready && current.generation == 8 && current.sequence == 3);
    const auto rebound_files = io.files;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Ready && current.generation == 8);
    assert(store.rebindCommittedGeneration(current, verify, 8, scratch, sizeof(scratch)) == HeadStoreResult::Ready && io.files == rebound_files);
    assert(store.rebindCommittedGeneration(current, verify, 0, scratch, sizeof(scratch)) == HeadStoreResult::Invalid && io.files == rebound_files);
    // Generations are local session epochs, not an ordering of author history.
    assert(store.rebindCommittedGeneration(current, verify, 2, scratch, sizeof(scratch)) == HeadStoreResult::Ready && current.generation == 2);
    io.files = committed_files;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Ready && current.sequence == 2);
    auto stale = next;
    stale.sequence = 1;
    stale.phase = HeadPhase::Signed;
    assert(store.advance(stale, next, verify, scratch, sizeof(scratch)) == HeadStoreResult::Conflict && io.files == committed_files);
    next = current;
    next.sequence = 3;
    next.revision = 2;
    next.phase = HeadPhase::Signed;
    std::memcpy(next.parent, current.hash, 32);
    next.hash[0] = 4;
    next.operation[0] = 5;
    io.short_write = true;
    assert(store.advance(current, next, verify, scratch, sizeof(scratch)) == HeadStoreResult::StorageFailure && !io.active);
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Corrupt && !current.sequence);
    io.short_write = false;
    io.files = committed_files;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Ready);
    assert(store.advance(current, next, verify, scratch, sizeof(scratch)) == HeadStoreResult::Ready);
    io.files["a"][0] ^= 1;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::Corrupt && !current.sequence);
    io.unavailable = true;
    assert(store.load(current, verify, scratch, sizeof(scratch)) == HeadStoreResult::StorageFailure && !current.sequence && !io.active);
    {
        Io jobs;
        PublicationJournalStore<Io> journal(jobs, "job.a", "job.b");
        static_assert(sizeof(journal) <= 24, "Publication store cannot own record buffers");
        PublicationJournal job, desired, proof;
        uint8_t buffer[kPublicationJournalBytes];
        assert(journal.load(job, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Missing);
        for (const auto field : {geocaching::ByteView{job.operation, 16}, {job.object, 32}, {job.revision_hash, 32}, {job.directory, 16}, {job.directory_epoch, 16}, {job.file_hash, 32}, {job.author_hash, 32}}) std::memset(const_cast<uint8_t*>(field.data), 1, field.size);
        job.sequence = 1;
        job.generation = 7;
        job.revision = 2;
        job.file_bytes = 750;
        assert(journal.initialize(job, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Ready);
        desired = job;
        desired.sequence = 2;
        desired.phase = PublicationPhase::Uploading;
        desired.offset = 320;
        assert(journal.advance(job, desired, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Ready);
        const auto durable = jobs.files;
        assert(journal.load(job, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Ready && job.sequence == 2 && job.offset == 320);
        auto stale = job;
        stale.sequence = 1;
        stale.phase = PublicationPhase::Accepted;
        stale.offset = 0;
        assert(journal.advance(stale, job, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Conflict && jobs.files == durable);
        desired = job;
        desired.sequence = 3;
        desired.offset = 640;
        jobs.short_write = true;
        assert(journal.advance(job, desired, proof, buffer, sizeof(buffer)) == PublicationStoreResult::StorageFailure && !jobs.active);
        assert(journal.load(job, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Corrupt && !job.sequence);
        jobs.short_write = false;
        jobs.files = durable;
        assert(journal.load(job, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Ready);
        jobs.files["job.a"][0] ^= 1;
        assert(journal.load(job, proof, buffer, sizeof(buffer)) == PublicationStoreResult::Corrupt && !job.sequence);
    }
}
