#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/file_integrity.h"
#include "track_sharing/image_source_binding_store.h"
#include <array>
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
    unsigned operations = 0, fail_at = 0;
    bool active = false, short_write = false;
    uint64_t reported_size = 0;
    bool fail() { return ++operations == fail_at; }
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        if (fail()) return {nullptr, OpenStatus::Unavailable};
        path = name;
        offset = 0;
        if (mode == Mode::Read && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        active = false;
        return !fail();
    }
    bool size(void*, uint64_t& size)
    {
        if (fail()) return false;
        size = reported_size ? reported_size : files[path].size();
        return true;
    }
    bool seek(void*, uint64_t at)
    {
        if (fail()) return false;
        offset = size_t(at);
        return offset <= files[path].size();
    }
    size_t read(void*, uint8_t* output, size_t length)
    {
        if (fail()) return 0;
        length = std::min(length, files[path].size() - offset);
        std::memcpy(output, files[path].data() + offset, length);
        offset += length;
        return length;
    }
    size_t write(void*, const uint8_t* input, size_t length)
    {
        if (fail()) return 0;
        if (short_write) length /= 2;
        files[path].assign(input, input + length);
        return length;
    }
    bool sync(void*) { return !fail(); }
    bool remove(const char* name)
    {
        assert(!active);
        return !fail() && files.erase(name) == 1;
    }
    bool rename(const char* from, const char* to)
    {
        assert(!active);
        if (fail() || !files.count(from) || files.count(to)) return false;
        files[to] = std::move(files[from]);
        files.erase(from);
        return true;
    }
};
using Result = track_sharing::ImageBindingResult;
const track_sharing::ImageBindingPaths paths = {"image.tmp", "image.jpg", "source.new", "source.meta", "thumb.bmp", "view.bmp"};
std::array<uint8_t, 32> digest(const std::vector<uint8_t>& bytes)
{
    std::array<uint8_t, 32> hash{};
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 stream(context);
    assert(stream.start() && stream.update(bytes.data(), bytes.size()) && stream.finish(hash.data()));
    return hash;
}
std::vector<uint8_t> record(const track_sharing::ImageSourceBindingView& value)
{
    std::vector<uint8_t> output(track_sharing::kImageSourceBindingBytes);
    assert(track_sharing::encodeImageSourceBinding(value, output.data(), output.size()));
    return output;
}
int main()
{
    const std::vector<uint8_t> old_image = {1, 2, 3}, new_image = {4, 5, 6, 7, 8}, old_reference = {'a'}, new_reference = {'b'};
    const auto old_file = digest(old_image), new_file = digest(new_image), old_source = digest(old_reference), new_source = digest(new_reference);
    // Independent Python hashlib SHA-256 fixture for bytes 04 05 06 07 08.
    const std::array<uint8_t, 32> golden = {0x01, 0x5d, 0xaf, 0x60, 0x13, 0x37, 0x6b, 0x77, 0x42, 0x1b, 0x7f, 0xb9, 0xd5, 0x65, 0x4d, 0x84, 0x4c, 0x8c, 0xba, 0x4f, 0x7a, 0x53, 0x26, 0xd8, 0x29, 0x42, 0x6d, 0x87, 0xfd, 0x0d, 0x1a, 0x5f};
    assert(new_file == golden);
    const track_sharing::ImageSourceBindingView old_binding = {{old_source.data(), 32}, {old_file.data(), 32}, 3}, expected = {{new_source.data(), 32}, {new_file.data(), 32}, 5};
    const auto old_record = record(old_binding), new_record = record(expected);
    assert(new_record.size() == 75 && !std::memcmp(new_record.data(), "TMSI", 4));
    for (size_t i = 0; i < new_record.size(); ++i)
    {
        auto corrupt = new_record;
        corrupt[i] ^= 1;
        track_sharing::ImageSourceBindingView view;
        assert(!track_sharing::decodeImageSourceBinding(corrupt.data(), corrupt.size(), view) && !view.file_bytes);
    }
    auto oversized = expected;
    oversized.file_bytes = track_sharing::kImageSourceByteLimit + 1;
    uint8_t scratch[128]{};
    assert(!track_sharing::encodeImageSourceBinding(oversized, scratch, sizeof(scratch)));
    oversized.file_bytes = 0;
    assert(!track_sharing::encodeImageSourceBinding(oversized, scratch, sizeof(scratch)));
    track_sharing::ImageSourceBindingView alias;
    assert(track_sharing::decodeImageSourceBinding(new_record.data(), new_record.size(), alias));
    auto aliased = new_record;
    track_sharing::ImageSourceBindingView aliased_view;
    assert(track_sharing::decodeImageSourceBinding(aliased.data(), aliased.size(), aliased_view));
    assert(!track_sharing::encodeImageSourceBinding(aliased_view, aliased.data(), aliased.size()));
    auto initial = [&]()
    {Io io;io.files={{paths.image,old_image},{paths.binding,old_record},{paths.candidate_image,new_image},{paths.preview,{9}},{paths.view,{10}}};return io; };
    unsigned terminal_operations = 0;
    for (unsigned failure = 0; failure < 50; ++failure)
    {
        auto io = initial();
        mbedtls_sha256_context context;
        platform::esp::storage::SnapshotStreamSha256 hash(context);
        track_sharing::FileIntegrityCheck<Io, decltype(hash)> verified(io, hash);
        assert(verified.begin(paths.candidate_image, new_image.size(), new_file.data()) == track_sharing::FileIntegrityResult::Pending);
        while (verified.active()) verified.step(scratch, sizeof(scratch));
        assert(verified.matchesSource(paths.candidate_image, 5, new_file.data()));
        assert(!verified.matchesSource("other.tmp", 5, new_file.data()));
        assert(!verified.matchesSource(paths.candidate_image, 4, new_file.data()));
        assert(!verified.matchesSource(paths.candidate_image, 5, old_file.data()));
        track_sharing::ImageSourceBindingStore<Io> store(io);
        static_assert(sizeof(store) == sizeof(void*), "Store retained metadata or buffers");
        io.operations = 0;
        io.fail_at = failure;
        const auto status = store.commit(paths, expected, verified, scratch, sizeof(scratch));
        assert(!io.active);
        if (!failure)
        {
            assert(status == Result::Ready);
            terminal_operations = io.operations;
        }
        else if (failure <= terminal_operations) assert(status != Result::Ready);
        else
        {
            assert(status == Result::Ready);
            break;
        }
        // Whatever failed, a published binding must describe its image bytes.
        if (io.files.count(paths.binding))
        {
            track_sharing::ImageSourceBindingView saved;
            assert(track_sharing::decodeImageSourceBinding(io.files.at(paths.binding).data(), 75, saved));
            assert(io.files.count(paths.image));
            const auto actual = digest(io.files.at(paths.image));
            assert(saved.file_bytes == io.files.at(paths.image).size() && !std::memcmp(saved.file_hash.data, actual.data(), 32));
            if (io.files.at(paths.image) == new_image) assert(!io.files.count(paths.preview) && !io.files.count(paths.view));
        }
        if (status == Result::Ready)
        {
            assert(io.files.at(paths.image) == new_image && io.files.at(paths.binding) == new_record);
            assert(!io.files.count(paths.candidate_image) && !io.files.count(paths.candidate_binding));
            // Reusing a proof after its candidate was renamed must not delete
            // the published image on retry.
            io.fail_at = 0;
            const auto before = io.files;
            assert(store.commit(paths, expected, verified, scratch, sizeof(scratch)) == Result::Unverified && io.files == before);
        }
    }
    auto io = initial();
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    track_sharing::FileIntegrityCheck<Io, decltype(hash)> unverified(io, hash);
    track_sharing::ImageSourceBindingStore<Io> store(io);
    const auto before = io.files;
    assert(store.commit(paths, expected, unverified, scratch, sizeof(scratch)) == Result::Unverified && io.files == before);
    assert(unverified.begin(paths.candidate_image, 5, new_file.data()) == track_sharing::FileIntegrityResult::Pending);
    while (unverified.active()) unverified.step(scratch, sizeof(scratch));
    auto alias_paths = paths;
    alias_paths.binding = "IMAGE.JPG";
    assert(store.commit(alias_paths, expected, unverified, scratch, sizeof(scratch)) == Result::Invalid && io.files == before);
    io.short_write = true;
    assert(store.commit(paths, expected, unverified, scratch, sizeof(scratch)) == Result::StorageFailure);
    assert(io.files.at(paths.image) == old_image && io.files.at(paths.binding) == old_record);
    io.short_write = false;
    track_sharing::ImageSourceBindingView loaded;
    assert(store.read(paths.binding, scratch, sizeof(scratch), loaded) == Result::Ready && track_sharing::imageSourceBindingMatches(loaded, old_binding));
    io.reported_size = uint64_t(UINT32_MAX) + 1 + 75;
    assert(store.read(paths.binding, scratch, sizeof(scratch), loaded) == Result::Corrupt && !loaded.file_bytes);
    io.reported_size = 0;
    io.files.erase(paths.binding);
    assert(store.read(paths.binding, scratch, sizeof(scratch), loaded) == Result::Missing && !loaded.file_bytes);
}
