#include "track_sharing/admit_publication_intent.h"
#include "track_sharing/load_publication_history.h"
#include "track_sharing/publication_candidate_reader.h"
#include "track_sharing/cancel_publication.h"
#include <array>
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
    std::string path, fail_create_path;
    bool active = false, fail_open = false, fail_read = false, fail_sync = false, fail_close = false, short_write = false;
    unsigned writes = 0;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        if (fail_open) return {nullptr, OpenStatus::Unavailable};
        path = name;
        if (mode == Mode::CreateTemporary && path == fail_create_path) return {nullptr, OpenStatus::Unavailable};
        if (mode == Mode::Read && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        return {this, OpenStatus::Ready};
    }
    bool size(void*, uint64_t& output)
    {
        assert(active);
        output = files[path].size();
        return true;
    }
    size_t read(void*, uint8_t* output, size_t size)
    {
        assert(active);
        if (fail_read) return 0;
        assert(files[path].size() >= size);
        std::memcpy(output, files[path].data(), size);
        return size;
    }
    size_t write(void*, const uint8_t* input, size_t size)
    {
        assert(active);
        ++writes;
        if (short_write) --size;
        files[path].assign(input, input + size);
        return size;
    }
    bool sync(void*)
    {
        assert(active);
        return !fail_sync;
    }
    bool close(void*)
    {
        assert(active);
        active = false;
        return !fail_close;
    }
};

int main()
{
    using namespace track_sharing;
    PublicationDirectory desired;
    std::memset(desired.delivery, 1, 16);
    std::memset(desired.public_key, 2, 64);
    std::memset(desired.epoch, 3, 16);
    desired.file_limit = 8388608;
    desired.chunk_limit = 1024;
    desired.query_limit = 32;
    std::array<uint8_t, kPublicationDirectoryBytes> bytes;
    assert(encodePublicationDirectory(desired, bytes.data(), bytes.size()));
    PublicationDirectory decoded;
    assert(decodePublicationDirectory(bytes.data(), bytes.size(), decoded));
    assert(publicationDirectoriesEqual(desired, decoded));
    for (size_t size = 0; size < bytes.size(); ++size) assert(!decodePublicationDirectory(bytes.data(), size, decoded));
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] ^= 1;
        assert(!decodePublicationDirectory(bytes.data(), bytes.size(), decoded));
        bytes[i] ^= 1;
    }
    assert(!encodePublicationDirectory(desired, reinterpret_cast<uint8_t*>(&desired), sizeof(desired)));
    assert(!decodePublicationDirectory(reinterpret_cast<const uint8_t*>(&decoded), kPublicationDirectoryBytes, decoded));
    auto invalid = desired;
    invalid.chunk_limit = 1025;
    assert(!publicationDirectoryValid(invalid));
    invalid = desired;
    invalid.query_limit = 0;
    assert(!publicationDirectoryValid(invalid));
    // Semantic limits remain mandatory even with a matching damage checksum.
    bytes[105] = 1;
    bytes[106] = 4;
    const auto semantic_crc = uploadJournalCrc(bytes.data(), 108);
    bytes[108] = uint8_t(semantic_crc);
    bytes[109] = uint8_t(semantic_crc >> 8);
    assert(!decodePublicationDirectory(bytes.data(), bytes.size(), decoded));
    assert(encodePublicationDirectory(desired, bytes.data(), bytes.size()));

    PublicationJournal job;
    std::memset(job.operation, 1, 16);
    std::memset(job.object, 1, 32);
    std::memset(job.revision_hash, 1, 32);
    std::memcpy(job.directory, desired.delivery, 16);
    std::memcpy(job.directory_epoch, desired.epoch, 16);
    std::memset(job.file_hash, 1, 32);
    std::memset(job.author_hash, 1, 32);
    job.sequence = job.generation = job.revision = 1;
    job.file_bytes = 4096;
    assert(publicationDirectoryMatches(desired, job));
    job.directory_epoch[0] ^= 1;
    assert(!publicationDirectoryMatches(desired, job));
    job.directory_epoch[0] ^= 1;
    invalid = desired;
    invalid.file_limit = 4095;
    assert(!publicationDirectoryMatches(invalid, job));

    Io io;
    PublicationDirectoryStore<Io> store(io, "/job.dir");
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::Ready);
    assert(!io.active && io.writes == 1);
    // Restart has no pool/USB owner and preserves exact public key and epoch.
    PublicationDirectoryStore<Io> restarted(io, "/job.dir");
    decoded = {};
    assert(restarted.load(decoded, bytes.data(), bytes.size()) == PublicationStoreResult::Ready);
    assert(publicationDirectoriesEqual(desired, decoded));
    assert(restarted.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::Ready && io.writes == 1);
    for (unsigned field = 0; field < 3; ++field)
    {
        auto changed = desired;
        if (field == 0) changed.public_key[0] ^= 1;
        if (field == 1) changed.epoch[0] ^= 1;
        if (field == 2) --changed.chunk_limit;
        assert(restarted.admit(changed, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::Conflict);
        assert(io.writes == 1);
    }
    io.fail_read = true;
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::StorageFailure);
    io.fail_read = false;
    io.fail_close = true;
    assert(store.load(decoded, bytes.data(), bytes.size()) == PublicationStoreResult::StorageFailure && !io.active);
    io.fail_close = false;
    io.fail_open = true;
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::StorageFailure);
    io.fail_open = false;
    assert(store.admit(desired, desired, bytes.data(), bytes.size()) == PublicationStoreResult::Invalid);
    assert(store.load(decoded, reinterpret_cast<uint8_t*>(&decoded), kPublicationDirectoryBytes) == PublicationStoreResult::Invalid);

    // A torn first write cannot be overwritten by a retry with a new directory.
    io.files.clear();
    io.short_write = true;
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::StorageFailure && !io.active);
    io.short_write = false;
    const auto writes = io.writes;
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::Corrupt && io.writes == writes);
    io.files.clear();
    io.fail_sync = true;
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::StorageFailure && !io.active);
    io.fail_sync = false;
    // Readback on the next admission may authorize only an identical record.
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::Ready);
    io.files["/job.dir"][21] ^= 1;
    assert(store.admit(desired, decoded, bytes.data(), bytes.size()) == PublicationStoreResult::Corrupt);

    // Durable pin precedes durable job; a restart in between resumes the same
    // operation without replacing its original directory evidence.
    Io admission_io;
    PublicationDirectoryStore<Io> pins(admission_io, "/op.dir");
    PublicationJournalStore<Io> jobs(admission_io, "/op.pua", "/op.pub");
    PublicationJournal current, verify;
    std::array<uint8_t, kPublicationJournalBytes> scratch;
    admission_io.short_write = true;
    assert(admitPublicationIntent(pins, jobs, desired, decoded, job, current, verify, scratch.data(), scratch.size()) == PublicationStoreResult::StorageFailure);
    assert(!admission_io.files.count("/op.pua"));
    admission_io.short_write = false;
    assert(admitPublicationIntent(pins, jobs, desired, decoded, job, current, verify, scratch.data(), scratch.size()) == PublicationStoreResult::Corrupt);
    assert(!admission_io.files.count("/op.pua"));
    admission_io.files.clear();
    assert(pins.admit(desired, decoded, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
    assert(!admission_io.files.count("/op.pua"));
    admission_io.fail_create_path = "/op.pua";
    assert(admitPublicationIntent(pins, jobs, desired, decoded, job, current, verify, scratch.data(), scratch.size()) == PublicationStoreResult::StorageFailure);
    assert(admission_io.files.count("/op.dir") && !admission_io.files.count("/op.pua") && !admission_io.active);
    admission_io.fail_create_path.clear();
    assert(admitPublicationIntent(pins, jobs, desired, decoded, job, current, verify, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
    assert(current.sequence == 1 && current.phase == PublicationPhase::Accepted);
    auto next = current;
    ++next.sequence;
    next.phase = PublicationPhase::Uploading;
    assert(jobs.advance(current, next, verify, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
    const auto admission_writes = admission_io.writes;
    assert(admitPublicationIntent(pins, jobs, desired, decoded, job, current, verify, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
    assert(current.sequence == 2 && current.phase == PublicationPhase::Uploading && admission_io.writes == admission_writes);
    auto changed_directory = desired;
    changed_directory.public_key[0] ^= 1;
    assert(admitPublicationIntent(pins, jobs, changed_directory, decoded, job, current, verify, scratch.data(), scratch.size()) == PublicationStoreResult::Conflict);
    assert(admission_io.writes == admission_writes);
    assert(admitPublicationIntent(pins, jobs, desired, decoded, job, current, current, scratch.data(), scratch.size()) == PublicationStoreResult::Invalid);
    next=current;++next.sequence;next.offset=320;
    assert(jobs.advance(current,next,verify,scratch.data(),scratch.size())==PublicationStoreResult::Ready);
    const auto history_files=admission_io.files;
    const auto history_writes=admission_io.writes;
    PublicationDirectoryStore<Io> restarted_pins(admission_io,"/op.dir");
    PublicationJournalStore<Io> restarted_jobs(admission_io,"/op.pua","/op.pub");
    assert(loadPublicationHistory(restarted_pins,restarted_jobs,desired,job,decoded,current,verify,scratch.data(),scratch.size())==PublicationStoreResult::Ready);
    assert(current.sequence==3 && current.offset==320 && current.phase==PublicationPhase::Uploading);
    assert(admission_io.files==history_files && admission_io.writes==history_writes && !admission_io.active);
    auto wrong_author=job;wrong_author.author_hash[0]^=1;
    assert(loadPublicationHistory(restarted_pins,restarted_jobs,desired,wrong_author,decoded,current,verify,scratch.data(),scratch.size())==PublicationStoreResult::Conflict);
    assert(!current.sequence && !verify.sequence && !decoded.file_limit);
    assert(loadPublicationHistory(restarted_pins,restarted_jobs,changed_directory,job,decoded,current,verify,scratch.data(),scratch.size())==PublicationStoreResult::Conflict);
    admission_io.files.erase("/op.pua");admission_io.files.erase("/op.pub");
    assert(loadPublicationHistory(restarted_pins,restarted_jobs,desired,job,decoded,current,verify,scratch.data(),scratch.size())==PublicationStoreResult::Missing);
    assert(admission_io.writes==history_writes && !current.sequence && !admission_io.active);
    {
        Io cancel_io;cancel_io.files=history_files;
        PublicationJournalStore<Io> cancel_jobs(cancel_io,"/op.pua","/op.pub");
        PublicationJournal cancelling,next_cancel,verify_cancel;
        assert(cancel_jobs.load(cancelling,verify_cancel,scratch.data(),scratch.size())==PublicationStoreResult::Ready);
        uint8_t author[32];std::memcpy(author,job.author_hash,32);
        assert(cancelPublicationBeforePublish(cancel_jobs,cancelling,next_cancel,verify_cancel,job.generation+1,{author,32},scratch.data(),scratch.size())==PublicationStoreResult::Conflict && !cancel_io.writes);
        cancel_io.fail_sync=true;
        assert(cancelPublicationBeforePublish(cancel_jobs,cancelling,next_cancel,verify_cancel,job.generation,{author,32},scratch.data(),scratch.size())==PublicationStoreResult::StorageFailure);
        assert(cancelling.sequence==3 && cancelling.offset==320 && cancelling.phase==PublicationPhase::Uploading);
        cancel_io.fail_sync=false;cancel_io.files=history_files;
        assert(cancelPublicationBeforePublish(cancel_jobs,cancelling,next_cancel,verify_cancel,job.generation,{author,32},scratch.data(),scratch.size())==PublicationStoreResult::Ready);
        assert(cancelling.sequence==4 && cancelling.offset==320 && cancelling.phase==PublicationPhase::Cancelled);
        const auto writes=cancel_io.writes;
        assert(cancelPublicationBeforePublish(cancel_jobs,cancelling,next_cancel,verify_cancel,job.generation,{author,32},scratch.data(),scratch.size())==PublicationStoreResult::Ready && cancel_io.writes==writes);
        cancelling.phase=PublicationPhase::Publishing;cancelling.offset=cancelling.file_bytes;
        assert(cancelPublicationBeforePublish(cancel_jobs,cancelling,next_cancel,verify_cancel,job.generation,{author,32},scratch.data(),scratch.size())==PublicationStoreResult::Conflict && cancel_io.writes==writes);
    }
    {
        PublicationSlotKey candidate;
        std::memcpy(candidate.object,job.object,32);std::memcpy(candidate.operation,job.operation,16);
        Io inventory_io;char canonical[kObjectPathCapacity];
        assert(resolveObjectPath({candidate.object,32},ObjectFile::PublicationDirectory,{candidate.operation,16},canonical,sizeof(canonical)));
        inventory_io.files[canonical]=history_files.at("/op.dir");
        assert(resolveObjectPath({candidate.object,32},ObjectFile::PublicationA,{candidate.operation,16},canonical,sizeof(canonical)));
        inventory_io.files[canonical]=history_files.at("/op.pua");
        assert(resolveObjectPath({candidate.object,32},ObjectFile::PublicationB,{candidate.operation,16},canonical,sizeof(canonical)));
        inventory_io.files[canonical]=history_files.at("/op.pub");
        PublicationCandidateReader<Io> reader(inventory_io);
        assert(reader.load(candidate,scratch.data(),scratch.size())==PublicationStoreResult::Ready);
        assert(reader.unverifiedJournal()->sequence==3 && reader.unverifiedJournal()->offset==320 && reader.unverifiedDirectory());
        candidate.slot=ObjectFile::PublicationB;
        assert(reader.load(candidate,scratch.data(),scratch.size())==PublicationStoreResult::Ready && !inventory_io.writes && !inventory_io.active);
        assert(reader.load(candidate,nullptr,0)==PublicationStoreResult::Invalid && !reader.unverifiedJournal());
        assert(reader.load(candidate,scratch.data(),scratch.size())==PublicationStoreResult::Ready);
        inventory_io.files[canonical][10]^=1;
        assert(reader.load(candidate,scratch.data(),scratch.size())==PublicationStoreResult::Corrupt && !reader.unverifiedJournal() && !reader.unverifiedDirectory());
        assert(!inventory_io.writes && !inventory_io.active);
    }
    admission_io.files=history_files;admission_io.files["/op.pub"][10]^=1;
    assert(loadPublicationHistory(restarted_pins,restarted_jobs,desired,job,decoded,current,verify,scratch.data(),scratch.size())==PublicationStoreResult::Corrupt);
    assert(admission_io.writes==history_writes && !current.sequence && !admission_io.active);
}
