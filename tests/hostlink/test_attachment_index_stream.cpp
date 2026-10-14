#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/attachment_index_file_check.h"
#include <cassert>
#include <string>
#include <vector>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
static uint8_t window[4352], kml_hash[32] = {1};
static track_sharing::AttachmentFingerprint fingerprints[64];
static std::vector<uint8_t> fixture(unsigned count = 3, size_t reference_bytes = 30, bool duplicate = false)
{
    std::vector<uint8_t> raw(track_sharing::kMaxAttachmentIndexBytes);
    geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
    assert(writer.array(4) && writer.unsignedInteger(1) && writer.binary({kml_hash, 32}) && writer.unsignedInteger(300) && writer.array(count));
    uint8_t hash[32] = {7};
    for (unsigned i = 0; i < count; ++i)
    {
        const auto id = duplicate && i == 1 ? "IMG0" : "img" + std::to_string(i);
        auto reference = "photos/" + std::to_string(i) + "/";
        reference.append(reference_bytes > reference.size() ? reference_bytes - reference.size() : 0, 'x');
        assert(writer.array(7) && writer.text(id) && writer.binary({hash, 32}) && writer.unsignedInteger(100) &&
               writer.text("image/png") && writer.text(reference) && writer.unsignedInteger(0) && writer.unsignedInteger(i % 2));
    }
    raw.resize(writer.size());
    return raw;
}
static bool stream(const std::vector<uint8_t>& raw, size_t chunk, unsigned count, unsigned& entries)
{
    track_sharing::AttachmentIndexStream parser(window, sizeof(window), fingerprints, 64, {kml_hash, 32}, 300, static_cast<uint32_t>(raw.size()), static_cast<uint8_t>(count));
    size_t offset = 0;
    entries = 0;
    while (offset < raw.size())
    {
        size_t consumed = 0;
        const auto result = parser.feed(raw.data() + offset, std::min(chunk, raw.size() - offset), consumed);
        offset += consumed;
        if (result == track_sharing::AttachmentStreamResult::Invalid) return false;
        if (result == track_sharing::AttachmentStreamResult::Entry)
        {
            const auto* entry = parser.entry();
            assert(entry && entry->id == "img" + std::to_string(entries));
            size_t paused = 99;
            assert(parser.feed(nullptr, 0, paused) == result && paused == 0 && parser.entry() == entry);
            ++entries;
            assert(parser.advance() && !parser.entry());
        }
        else assert(consumed > 0);
    }
    assert(parser.packageBytes() == raw.size() + 300 + entries * 100);
    return parser.finish();
}
struct Io
{
    enum class Mode
    {
        Read
    };
    struct OpenResult
    {
        void* handle;
    };
    std::vector<uint8_t> bytes;
    bool opened = false, fail_open = false, fail_close = false, fail_read = false, fail_size = false;
    size_t offset = 0, opens = 0, reads = 0;
    OpenResult open(const char*, Mode)
    {
        assert(!opened);
        ++opens;
        if (fail_open) return {nullptr};
        opened = true;
        return {this};
    }
    bool close(void*)
    {
        assert(opened);
        opened = false;
        return !fail_close;
    }
    bool size(void*, uint64_t& value)
    {
        value = bytes.size() + (fail_size ? 1 : 0);
        return true;
    }
    bool seek(void*, uint64_t value)
    {
        offset = static_cast<size_t>(value);
        return offset <= bytes.size();
    }
    size_t read(void*, uint8_t* output, size_t length)
    {
        assert(opened && length <= 320 && offset + length <= bytes.size());
        ++reads;
        if (fail_read) return 0;
        std::memcpy(output, bytes.data() + offset, length);
        return length;
    }
};
int main()
{
    unsigned entries = 0;
    const auto small = fixture();
    for (size_t chunk = 1; chunk <= small.size(); ++chunk) assert(stream(small, chunk, 3, entries) && entries == 3);
    const auto large = fixture(64, 4096);
    for (size_t chunk : {size_t(1), size_t(17), size_t(320), size_t(4096), large.size()}) assert(stream(large, chunk, 64, entries) && entries == 64);
    assert(!stream(fixture(3, 30, true), 1, 3, entries));
    auto trailing = small;
    trailing.push_back(0);
    assert(!stream(trailing, 320, 3, entries));
    auto truncated = small;
    truncated.pop_back();
    assert(!stream(truncated, 1, 3, entries));
    auto wrong_header = small;
    wrong_header[4] ^= 1;
    assert(!stream(wrong_header, 1, 3, entries));
    assert(!stream(small, 1, 2, entries));
    auto wrong_scalar = small;
    wrong_scalar[wrong_scalar.size() - 1] = 0xc3;
    assert(!stream(wrong_scalar, 1, 3, entries));
    track_sharing::AttachmentIndexStream no_window(nullptr, 0, fingerprints, 64, {kml_hash, 32}, 300, small.size(), 3);
    assert(!no_window.admitted() && !no_window.finish());
    track_sharing::AttachmentIndexStream alias(window, sizeof(window), reinterpret_cast<track_sharing::AttachmentFingerprint*>(window), 64, {kml_hash, 32}, 300, small.size(), 3);
    assert(!alias.admitted());
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    uint8_t digest[32];
    assert(hash.start() && hash.update(large.data(), large.size()) && hash.finish(digest));
    hash.reset();
    const track_sharing::AttachmentIndexBinding binding{{digest, 32}, static_cast<uint32_t>(large.size()), 64};
    using Check = track_sharing::AttachmentIndexFileCheck<Io, decltype(hash)>;
    using Result = track_sharing::AttachmentIndexFileResult;
    static_assert(sizeof(Check) <= 768, "Index job must remain a small admitted workspace");
    for (unsigned fault = 0; fault < 6; ++fault)
    {
        Io io;
        io.bytes = large;
        io.fail_open = fault == 1;
        io.fail_close = fault == 2;
        io.fail_read = fault == 3;
        io.fail_size = fault == 4;
        if (fault == 5) io.bytes[io.bytes.size() - 2] = 1; // role still valid, wrong digest
        Check check(io, hash, window, sizeof(window), fingerprints, 64, binding, {kml_hash, 32}, 300);
        assert(check.begin("index") == Result::Pending && check.begin("index") == Result::Busy);
        Result result;
        unsigned seen = 0;
        do
        {
            result = check.step();
            assert(!io.opened);
            if (result == Result::Entry)
            {
                assert(!check.ready() && check.entry());
                const auto reads = io.reads;
                assert(check.step() == Result::Entry && io.reads == reads);
                ++seen;
                assert(check.advance());
            }
        } while (result == Result::Pending || result == Result::Entry);
        assert(result == (fault == 0 ? Result::Ready : fault == 5 ? Result::HashMismatch
                                                                  : Result::StorageFailure));
        if (!fault)
        {
            assert(seen == 64 && check.ready() && check.matchesSource("index", binding));
            assert(!check.matchesSource("other", binding));
        }
        else assert(!check.ready());
    }
    Io io;
    io.bytes = large;
    Check cancelled(io, hash, window, sizeof(window), fingerprints, 64, binding, {kml_hash, 32}, 300);
    assert(cancelled.begin("index") == Result::Pending && cancelled.step() == Result::Pending);
    assert(cancelled.cancel() == Result::Cancelled && !cancelled.ready() && !io.opened);
    Check invalid(io, hash, nullptr, 0, fingerprints, 64, binding, {kml_hash, 32}, 300);
    const auto before = io.opens;
    assert(invalid.begin("index") != Result::Pending && invalid.step() == Result::Invalid && io.opens == before);
}
