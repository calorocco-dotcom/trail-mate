#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/upload_chunk_writer.h"
#include "track_sharing/upload_resources.h"
#include "track_sharing/upload_seal_task.h"
#include "track_sharing/upload_session.h"
#include "track_sharing/uploaded_manifest_reader.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
struct SealIo
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
    std::string path;
    size_t position = 0;
    bool active = false, short_log = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        position = 0;
        if (mode != Mode::CreateTemporary && !files.count(path)) return {nullptr, OpenStatus::Missing};
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
    bool seek(void*, uint64_t value)
    {
        position = size_t(value);
        return position <= files[path].size();
    }
    size_t read(void*, uint8_t* output, size_t count)
    {
        assert(count <= 320);
        count = std::min(count, files[path].size() - position);
        std::memcpy(output, files[path].data() + position, count);
        position += count;
        return count;
    }
    size_t write(void*, const uint8_t* input, size_t count)
    {
        if (short_log && path != "data") count /= 2;
        files[path].resize(std::max(files[path].size(), position + count));
        std::memcpy(files[path].data() + position, input, count);
        position += count;
        return count;
    }
    bool sync(void*) { return true; }
};
int main()
{
    using namespace track_sharing;
    const std::string content(750, 'x');
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    uint8_t digest[32], scratch[320], author[32] = {2};
    uint32_t generation = 7;
    assert(hash.start() && hash.update(reinterpret_cast<const uint8_t*>(content.data()), content.size()) && hash.finish(digest));
    for (unsigned mode = 0; mode < 6; ++mode)
    {
        SealIo io;
        io.files["manifest"] = std::vector<uint8_t>(content.begin(), content.end());
        UploadJournal journal;
        journal.id[0] = 1;
        journal.sequence = 1;
        journal.generation = generation;
        journal.total = journal.durable_offset = uint32_t(content.size());
        journal.phase = UploadPhase::Sealed;
        std::memcpy(journal.author_hash, author, 32);
        std::memcpy(journal.file_hash, digest, 32);
        uint8_t raw[4096]{};
        UploadedManifestReader<SealIo, decltype(hash), 1100> receipt_reader(io, hash, journal, generation, author);
        static_assert(sizeof(receipt_reader) == sizeof(UploadedManifestReader<SealIo, decltype(hash)>), "Receipt bound must not add runtime storage");
        auto oversized = journal;
        oversized.total = oversized.durable_offset = 1101;
        UploadedManifestReader<SealIo, decltype(hash), 1100> rejected(io, hash, oversized, generation, author);
        assert(rejected.begin("manifest", raw, sizeof(raw)) == FileIntegrityResult::Invalid && !io.active);
        UploadedManifestReader<SealIo, decltype(hash)> reader(io, hash, journal, generation, author);
        static_assert(sizeof(reader) <= 128, "Reader must borrow manifest storage");
        assert(reader.begin("manifest", raw, sizeof(raw)) == FileIntegrityResult::Pending && !reader.bytes().size);
        if (mode >= 3)
        {
            const auto preserved = io.files;
            const auto saved_journal = journal;
            uint8_t* invalid_scratch = mode == 3 ? reinterpret_cast<uint8_t*>(&reader) : mode == 4 ? reinterpret_cast<uint8_t*>(&journal)
                                                                                                   : author;
            assert(reader.step(invalid_scratch, 32) == FileIntegrityResult::Invalid && !reader.bytes().size && !io.active);
            assert(io.files == preserved && !std::memcmp(&saved_journal, &journal, sizeof(journal)) && author[0] == 2 && generation == 7);
            continue;
        }
        if (mode == 1) io.files["manifest"][500] ^= 1;
        assert(reader.step(scratch, sizeof(scratch)) == FileIntegrityResult::Pending && !io.active);
        if (mode == 2) ++generation;
        FileIntegrityResult result;
        do
        {
            result = reader.step(scratch, sizeof(scratch));
            assert(!io.active);
        } while (result == FileIntegrityResult::Pending);
        assert(result == (mode == 0 ? FileIntegrityResult::Ready : mode == 1 ? FileIntegrityResult::HashMismatch
                                                                             : FileIntegrityResult::Invalid));
        if (mode == 0)
        {
            assert(reader.bytes().size == content.size() && !std::memcmp(raw, content.data(), content.size()));
            ++generation;
            assert(!reader.bytes().size);
            --generation;
        }
        else
        {
            assert(!reader.bytes().size);
            for (size_t i = 0; i < content.size(); ++i) assert(raw[i] == 0);
        }
        if (mode == 2) --generation;
    }
    for (const auto kind : {UploadKind::Kml, UploadKind::Manifest, UploadKind::IpReceipt})
    {
        SealIo io;
        uint8_t id[16] = {5};
        using Resources = UploadResources<SealIo, decltype(hash)>;
        static_assert(sizeof(Resources) <= 768, "Upload resource aggregate exceeded transient admission");
        Resources owner(io, hash, id, generation, author, kind);
        assert(std::string(owner.candidatePath()).find(uploadDirectory(kind)) == 0);
        assert(owner.recover(scratch, sizeof(scratch)) == JournalStoreResult::Missing);
        io.files[owner.candidatePath()] = {};
        if (kind != UploadKind::Kml)
        {
            const auto unchanged = io.files;
            assert(owner.initialize(uploadByteLimit(kind) + 1, digest, scratch, sizeof(scratch)) == JournalStoreResult::Invalid);
            assert(io.files == unchanged && !io.active);
        }
        assert(owner.initialize(750, digest, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
        uint8_t session_id[16] = {1}, other_session[16] = {2};
        UploadSession<Resources> session(owner, session_id, 9, 100);
        assert(session.append(other_session, 9, 101, id, 0, reinterpret_cast<const uint8_t*>(content.data()), 320, scratch, sizeof(scratch)) == JournalStoreResult::Conflict);
        assert(io.files[owner.candidatePath()].empty());
        for (uint32_t offset = 0; offset < 750; offset += 320)
            assert(session.append(session_id, 9, 102, id, offset, reinterpret_cast<const uint8_t*>(content.data() + offset), std::min(size_t(320), 750 - offset), scratch, sizeof(scratch)) == JournalStoreResult::Ready);
        assert(session.beginSeal(session_id, 9, 103, scratch, sizeof(scratch)) == UploadSealResult::Pending);
        assert(session.advance(9, 104, true, scratch, sizeof(scratch)) == UploadSealResult::Pending);
        const auto saved_files = io.files;
        assert(session.advance(9, 105, false, scratch, sizeof(scratch)) == UploadSealResult::Cancelled && session.releasable());
        assert(io.files == saved_files && owner.metadata().phase == UploadPhase::Checking && !io.active);
        assert(session.beginSeal(session_id, 9, 106, scratch, sizeof(scratch)) == UploadSealResult::Conflict);
        Resources resumed(io, hash, id, generation, author, kind);
        assert(resumed.recover(scratch, sizeof(scratch)) == JournalStoreResult::Ready && resumed.metadata().phase == UploadPhase::Checking);
        UploadSession<Resources> new_session(resumed, other_session, 9, 200);
        assert(new_session.beginSeal(other_session, 9, 201, scratch, sizeof(scratch)) == UploadSealResult::Pending);
        UploadSealResult result;
        do
        {
            result = new_session.advance(9, 202, true, scratch, sizeof(scratch));
            assert(!io.active);
        } while (result == UploadSealResult::Pending);
        assert(result == UploadSealResult::Ready && resumed.metadata().phase == UploadPhase::Sealed);
        Resources recovered(io, hash, id, generation, author, kind);
        assert(recovered.recover(scratch, sizeof(scratch)) == JournalStoreResult::Ready && recovered.metadata().phase == UploadPhase::Sealed);
        generation = 8;
        Resources wrong(io, hash, id, generation, author, kind);
        assert(wrong.recover(scratch, sizeof(scratch)) == JournalStoreResult::Conflict && wrong.metadata().sequence == 0);
        generation = 7;
    }
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        SealIo io;
        io.files["data"] = {};
        UploadJournal current, verify;
        current.id[0] = 1;
        std::memcpy(current.author_hash, author, 32);
        std::memcpy(current.file_hash, digest, 32);
        current.generation = generation;
        current.total = 750;
        current.sequence = 1;
        UploadJournalStore<SealIo> store(io, "a", "b");
        assert(store.initialize(current, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
        UploadChunkWriter<SealIo, decltype(store)> writer(io, store, current, verify, "data", generation, author);
        for (uint32_t offset = 0; offset < 750; offset += 320)
            assert(writer.append(current.id, offset, reinterpret_cast<const uint8_t*>(content.data() + offset), std::min(size_t(320), 750 - offset), scratch, sizeof(scratch)) == JournalStoreResult::Ready);
        assert(io.files["data"] == std::vector<uint8_t>(content.begin(), content.end()));
        if (mode == 1) io.files["data"][500] ^= 1;
        using Check = FileIntegrityCheck<SealIo, decltype(hash)>;
        using Task = UploadSealTask<Check, decltype(store)>;
        Check check(io, hash);
        Task task(check, store, current, verify, generation, author);
        assert(task.begin("data", scratch, sizeof(scratch)) == UploadSealResult::Pending && current.phase == UploadPhase::Checking);
        if (mode == 2) io.short_log = true;
        UploadSealResult result;
        do
        {
            result = task.step(scratch, sizeof(scratch));
            assert(!io.active);
        } while (result == UploadSealResult::Pending);
        assert(result == (mode == 0 ? UploadSealResult::Ready : mode == 1 ? UploadSealResult::HashMismatch
                                                                          : UploadSealResult::StorageFailure));
        assert(store.load(current, verify, scratch, sizeof(scratch)) == JournalStoreResult::Ready);
        assert(current.phase == (mode == 0 ? UploadPhase::Sealed : mode == 1 ? UploadPhase::Failed
                                                                             : UploadPhase::Checking));
        if (mode == 2)
        {
            io.short_log = false;
            Check resumed(io, hash);
            Task retry(resumed, store, current, verify, generation, author);
            assert(retry.begin("data", scratch, sizeof(scratch)) == UploadSealResult::Pending);
            do
            {
                result = retry.step(scratch, sizeof(scratch));
                assert(!io.active);
            } while (result == UploadSealResult::Pending);
            assert(result == UploadSealResult::Ready && current.phase == UploadPhase::Sealed);
        }
    }
}
