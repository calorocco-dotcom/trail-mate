#include "platform/esp/common/storage/snapshot_file_response.h"
#include "platform/esp/common/storage/stdio_record_file_io.h"
#include "platform/esp/common/storage/track_snapshot_file_storage.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(TRAIL_MATE_REAL_SNAPSHOT_HASH)
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* buffer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(buffer);
    while (length--) *bytes++ = 0;
}
#endif
#endif

using namespace platform::esp::storage;
using namespace hostlink::management;
class Io : public RecordFileIo
{
  public:
    OpenResult open(const char* path, Mode mode) override
    {
        auto result = real.open(path, mode);
        if (result.handle)
        {
            ++handles;
            assert(handles == 1);
        }
        return result;
    }
    bool close(Handle file) override
    {
        --handles;
        return real.close(file);
    }
    bool size(Handle file, uint64_t& bytes) override { return real.size(file, bytes); }
    bool seek(Handle file, uint64_t offset) override { return real.seek(file, offset); }
    size_t read(Handle file, void* out, size_t bytes) override { return real.read(file, out, short_read ? bytes - 1 : bytes); }
    size_t write(Handle file, const void* data, size_t bytes) override { return real.write(file, data, short_write ? bytes - 1 : bytes); }
    bool sync(Handle file) override { return !sync_fail && real.sync(file); }
    bool publish(const char* temporary, const char* destination) override { return real.publish(temporary, destination); }
    StdioRecordFileIo real;
    int handles = 0;
    bool short_read = false, short_write = false, sync_fail = false;
};
#if defined(TRAIL_MATE_REAL_SNAPSHOT_HASH)
class Hash : public SnapshotStreamHash
{
  public:
    Hash() : hash(context) {}
    bool start() override { return hash.start(); }
    bool update(const uint8_t* data, size_t bytes) override { return hash.update(data, bytes); }
    bool finish(uint8_t out[32]) override { return hash.finish(out); }
    void reset() override { hash.reset(); }

  private:
    mbedtls_sha256_context context;
    SnapshotStreamSha256 hash;
};
#else
// Not cryptography: validates that the copy path feeds the stream in order.
// Real SHA-256 and physical SD durability require the platform integration.
class Hash : public SnapshotStreamHash
{
  public:
    bool start() override
    {
        count = 0;
        return true;
    }
    bool update(const uint8_t* data, size_t bytes) override
    {
        for (size_t i = 0; i < bytes; ++i) assert(data[i] == uint8_t((count + i) % 251));
        count += bytes;
        return true;
    }
    bool finish(uint8_t out[32]) override
    {
        std::memset(out, 7, 32);
        return true;
    }
    void reset() override { count = 0; }
    size_t count = 0;
};
#endif
class Lease : public SnapshotFileLease
{
  public:
    explicit Lease(const std::filesystem::path& root) : source((root / "source.bin").string()), temporary((root / "snapshot.new").string()), destination((root / "snapshot.bin").string()) {}
    SnapshotStorageResult acquire(const uint8_t[16], uint32_t, uint8_t) override
    {
        owned = true;
        valid_source = true;
        return SnapshotStorageResult::Ok;
    }
    bool valid() const override { return owned && valid_source; }
    const char* sourcePath() const override { return source.c_str(); }
    const char* temporaryPath() const override { return temporary.c_str(); }
    bool commit(const TrackSnapshotMetadata& metadata) override
    {
        assert(!metadata.sealed);
        if (commit_fail) return false;
        std::filesystem::rename(temporary, destination);
        committed = true;
        return true;
    }
    bool discard() override
    {
        if (discard_fail) return false;
        std::filesystem::remove(temporary);
        return true;
    }
    void release() override { owned = false; }
    std::string source, temporary, destination;
    bool owned = false, valid_source = true, committed = false, commit_fail = false, discard_fail = false;
};
static uint8_t scratch[320], response[448], id[16] = {1};
int main(int argc, char** argv)
{
    assert(argc > 0);
    const auto root = std::filesystem::absolute(argv[0]).parent_path() / "snapshot-file-fixture";
    std::filesystem::create_directories(root);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::filesystem::remove_all(path); }
    } cleanup{root};
    for (int scenario = 0; scenario < 7; ++scenario)
    {
        Io io;
        Hash hash;
        Lease lease(root);
        std::filesystem::remove(lease.destination);
        {
            std::ofstream source(lease.source, std::ios::binary);
            for (int i = 0; i < 750; ++i) source.put(char(i % 251));
        }
        TrackSnapshotFileStorage storage(io, hash, lease);
        TrackSnapshotBuilder builder(storage);
        TrackSnapshotMetadata metadata;
        assert(builder.begin(id, 750, 3) == SnapshotBuildResult::Pending && io.handles == 0);
        io.short_read = scenario == 1;
        io.short_write = scenario == 2;
        io.sync_fail = scenario == 3;
        if (scenario == 4) lease.valid_source = false;
        lease.commit_fail = scenario == 5;
        if (scenario == 6) assert(builder.cancel() == SnapshotBuildResult::Pending);
        SnapshotBuildResult result = SnapshotBuildResult::Pending;
        for (unsigned steps = 0; result == SnapshotBuildResult::Pending && steps < 10; ++steps)
        {
            result = builder.step(scratch, sizeof(scratch), metadata);
            assert(io.handles == 0);
        }
        assert(!lease.owned && !std::filesystem::exists(lease.temporary));
        if (scenario == 0)
        {
            assert(result == SnapshotBuildResult::Ready && metadata.sealed && lease.committed);
            const auto container = io.open(lease.destination.c_str(), RecordFileIo::Mode::Read);
            TrackSnapshotMetadata restored;
            assert(container.handle && readSnapshotContainerHeader(io, container.handle, restored));
            assert(restored.sealed && restored.bytes == metadata.bytes && restored.format == metadata.format && !std::memcmp(restored.hash, metadata.hash, 32));
            assert(io.close(container.handle) && io.handles == 0);
            std::ifstream saved(lease.destination, std::ios::binary);
            saved.seekg(kSnapshotContainerHeaderBytes);
            const std::string bytes((std::istreambuf_iterator<char>(saved)), {});
            assert(bytes.size() == 750);
            for (size_t i = 0; i < bytes.size(); ++i) assert(uint8_t(bytes[i]) == i % 251);
            saved.close();
            TrackSnapshotRequest request;
            std::memcpy(request.id, metadata.id, 16);
            request.maximum = 320;
            size_t written = 0;
            for (const auto offset : {0U, 320U, 640U})
            {
                request.offset = offset;
                request.has_hash = offset != 0;
                std::memcpy(request.expected_hash, metadata.hash, 32);
                assert(readSnapshotFileResponse(io, lease.destination.c_str(), request, restored, response, sizeof(response), written) == SnapshotReadResult::Ok && io.handles == 0);
                geocaching::protocol::CmpReader reader({response, written});
                size_t fields = 0;
                uint64_t value = 0;
                geocaching::ByteView data;
                assert(reader.array(fields, 6) && fields == 6 && reader.binary(data, 16) && !std::memcmp(data.data, id, 16));
                assert(reader.unsignedInteger(value) && value == 750 && reader.binary(data, 32));
                assert(reader.unsignedInteger(value) && value == 3 && reader.binary(data, 320));
                assert(data.size == std::min<uint32_t>(320, 750 - offset));
                for (size_t i = 0; i < data.size; ++i) assert(data.data[i] == (offset + i) % 251);
                assert(reader.unsignedInteger(value) && value == offset && reader.finished());
#if defined(TRAIL_MATE_REAL_SNAPSHOT_HASH)
                std::printf("chunk:");
                for (size_t i = 0; i < written; ++i) std::printf("%02x", response[i]);
                std::printf("\n");
#endif
            }
            request.expected_hash[0] ^= 1;
            assert(readSnapshotFileResponse(io, lease.destination.c_str(), request, restored, response, sizeof(response), written) == SnapshotReadResult::Conflict && written == 0 && io.handles == 0);
            request.expected_hash[0] ^= 1;
            request.offset = 750;
            assert(readSnapshotFileResponse(io, lease.destination.c_str(), request, restored, response, sizeof(response), written) == SnapshotReadResult::InvalidRequest && written == 0);
            request.offset = 0;
            assert(readSnapshotFileResponse(io, lease.destination.c_str(), request, restored, response, 10, written) == SnapshotReadResult::Capacity && written == 0 && io.handles == 0);
            for (const auto offset : {0U, 4U, 20U, 52U, 56U, 57U, 62U})
            {
                const auto file = io.open(lease.destination.c_str(), RecordFileIo::Mode::Update);
                uint8_t original = 0;
                assert(file.handle && io.seek(file.handle, offset) && io.read(file.handle, &original, 1) == 1);
                const uint8_t corrupt = original ^ 1;
                assert(io.seek(file.handle, offset) && io.write(file.handle, &corrupt, 1) == 1 && io.close(file.handle));
                const auto read = io.open(lease.destination.c_str(), RecordFileIo::Mode::Read);
                assert(read.handle && !readSnapshotContainerHeader(io, read.handle, restored) && !restored.sealed && io.close(read.handle));
                const auto repair = io.open(lease.destination.c_str(), RecordFileIo::Mode::Update);
                assert(repair.handle && io.seek(repair.handle, offset) && io.write(repair.handle, &original, 1) == 1 && io.close(repair.handle));
            }
            std::filesystem::resize_file(lease.destination, kSnapshotContainerHeaderBytes + metadata.bytes - 1);
            const auto truncated = io.open(lease.destination.c_str(), RecordFileIo::Mode::Read);
            assert(truncated.handle && !readSnapshotContainerHeader(io, truncated.handle, restored) && !restored.sealed && io.close(truncated.handle));
#if defined(TRAIL_MATE_REAL_SNAPSHOT_HASH)
            for (auto byte : metadata.hash) std::printf("%02x", byte);
            std::printf("\n");
#endif
        }
        else
        {
            assert(result == (scenario == 6 ? SnapshotBuildResult::Cancelled : SnapshotBuildResult::Failed));
            assert(!metadata.sealed && !lease.committed && !std::filesystem::exists(lease.destination));
        }
    }
    {
        Io io;
        Hash hash;
        Lease lease(root);
        lease.temporary = lease.source;
        TrackSnapshotFileStorage storage(io, hash, lease);
        assert(storage.begin(id, 750, 3) == SnapshotStorageResult::Failed);
        assert(storage.abort() == SnapshotStorageResult::Ok && !lease.owned && io.handles == 0);
        assert(std::filesystem::file_size(lease.source) == 750);
    }
#if defined(TRAIL_MATE_REAL_SNAPSHOT_HASH)
    {
        Io io;
        Hash hash;
        Lease lease(root);
        std::filesystem::remove(lease.destination);
        {
            std::ofstream source(lease.source, std::ios::binary);
            for (uint32_t i = 0; i < kTrackSnapshotMaxBytes; ++i) source.put(char(i % 251));
        }
        TrackSnapshotFileStorage storage(io, hash, lease);
        TrackSnapshotBuilder builder(storage);
        TrackSnapshotMetadata metadata;
        assert(builder.begin(id, kTrackSnapshotMaxBytes, 3) == SnapshotBuildResult::Pending);
        SnapshotBuildResult result = SnapshotBuildResult::Pending;
        unsigned steps = 0;
        while (result == SnapshotBuildResult::Pending && steps++ < kTrackSnapshotMaxBytes / 320 + 3)
        {
            result = builder.step(scratch, sizeof(scratch), metadata);
            assert(io.handles == 0);
        }
        assert(result == SnapshotBuildResult::Ready && metadata.sealed && !lease.owned);
        assert(std::filesystem::file_size(lease.destination) == kSnapshotContainerHeaderBytes + kTrackSnapshotMaxBytes);
        std::ifstream saved(lease.destination, std::ios::binary);
        saved.seekg(kSnapshotContainerHeaderBytes);
        for (uint32_t i = 0; i < kTrackSnapshotMaxBytes; ++i) assert(uint8_t(saved.get()) == i % 251);
        assert(saved.get() == std::char_traits<char>::eof());
        for (auto byte : metadata.hash) std::printf("%02x", byte);
        std::printf("\n");
    }
#endif
    return 0;
}
