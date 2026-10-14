#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/route_file_check.h"
#include <cassert>
#include <cstring>
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
        Read
    };
    struct OpenResult
    {
        void* handle;
    };
    std::vector<uint8_t> bytes;
    size_t offset = 0, maximum_read = 0;
    bool opened = false, fail_read = false, fail_close = false;
    OpenResult open(const char*, Mode)
    {
        assert(!opened);
        opened = true;
        return {this};
    }
    bool close(void*)
    {
        opened = false;
        return !fail_close;
    }
    bool size(void*, uint64_t& result)
    {
        result = bytes.size();
        return true;
    }
    bool seek(void*, uint64_t value)
    {
        offset = size_t(value);
        return offset <= bytes.size();
    }
    size_t read(void*, uint8_t* output, size_t length)
    {
        maximum_read = std::max(maximum_read, length);
        if (fail_read) return 0;
        assert(offset + length <= bytes.size());
        std::memcpy(output, bytes.data() + offset, length);
        return length;
    }
};
static char names[2048];
static uint8_t scratch[320];
int main()
{
    const std::string valid = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><name>" + std::string(1400, 'x') + "</name><LineString><coordinates>120,30 120.01,30.01</coordinates></LineString></Placemark></kml>";
    using Result = track_sharing::FileIntegrityResult;
    auto run = [&](const std::string& text, unsigned fault, Result expected)
    {
        Io io;
        io.bytes.assign(text.begin(), text.end());
        mbedtls_sha256_context context;
        platform::esp::storage::SnapshotStreamSha256 hash(context);
        uint8_t digest[32];
        assert(hash.start() && hash.update(io.bytes.data(), io.bytes.size()) && hash.finish(digest));
        if (fault == 1) digest[0] ^= 1;
        io.fail_read = fault == 2;
        io.fail_close = fault == 3;
        track_sharing::RouteFileCheck<Io, decltype(hash)> check(io, hash, names, sizeof(names));
        assert(check.begin("sealed.kml", uint32_t(io.bytes.size()), digest) == Result::Pending);
        assert(check.begin("sealed.kml", uint32_t(io.bytes.size()), digest) == Result::Busy);
        Result result = Result::Pending;
        unsigned steps = 0;
        while (result == Result::Pending && steps++ < 100) result = check.step(scratch, sizeof(scratch));
        assert(result == expected && !io.opened && io.maximum_read <= 320);
        assert(check.ready() == (expected == Result::Ready));
        if (expected == Result::Ready) assert(check.geometry() && check.geometry()->points == 2 && steps > 1);
        else assert(!check.geometry());
    };
    run(valid, 0, Result::Ready);
    run(valid, 1, Result::HashMismatch);
    run(valid, 2, Result::StorageFailure);
    run(valid, 3, Result::StorageFailure);
    run("<!DOCTYPE kml><kml xmlns=\"http://www.opengis.net/kml/2.2\"/>", 0, Result::Invalid);
    run("<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><Point><coordinates>999,30</coordinates></Point></Placemark></kml>", 0, Result::Invalid);
    run("<kml xmlns=\"http://www.opengis.net/kml/2.2\"/>", 0, Result::Invalid);
    Io io;
    io.bytes.assign(valid.begin(), valid.end());
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    uint8_t digest[32]{};
    track_sharing::RouteFileCheck<Io, decltype(hash)> check(io, hash, names, sizeof(names));
    assert(check.begin("sealed.kml", uint32_t(io.bytes.size()), digest) == Result::Pending);
    assert(check.step(scratch, sizeof(scratch)) == Result::Pending);
    assert(check.cancel() == Result::Cancelled && !check.ready());
    assert(check.step(scratch, sizeof(scratch)) == Result::Cancelled && !io.opened);
}
