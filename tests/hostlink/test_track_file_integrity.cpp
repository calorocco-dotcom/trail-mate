#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/file_integrity.h"
#include <cassert>
#include <cstdio>
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
    size_t offset = 0;
    bool opened = false, short_read = false, close_fail = false;
    OpenResult open(const char*, Mode)
    {
        assert(!opened);
        opened = true;
        return {this};
    }
    bool close(void*)
    {
        opened = false;
        return !close_fail;
    }
    bool size(void*, uint64_t& value)
    {
        value = bytes.size();
        return true;
    }
    bool seek(void*, uint64_t value)
    {
        offset = size_t(value);
        return offset <= bytes.size();
    }
    size_t read(void*, uint8_t* output, size_t length)
    {
        assert(length <= 320);
        if (short_read) return 0;
        std::memcpy(output, bytes.data() + offset, length);
        return length;
    }
};
int main()
{
    Io io;
    for (unsigned i = 0; i < 750; ++i) io.bytes.push_back(uint8_t(i % 251));
    const char* hex = "c6f564348770880184e0ec9f58c70d4fc8240c38171a7c3278c9fd0bb54d2d72";
    uint8_t expected[32], scratch[320];
    for (unsigned i = 0; i < 32; ++i)
    {
        unsigned byte = 0;
        std::sscanf(hex + i * 2, "%2x", &byte);
        expected[i] = uint8_t(byte);
    }
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    track_sharing::FileIntegrityCheck<Io, decltype(hash)> check(io, hash);
    static_assert(sizeof(check) <= 80, "Integrity check grew a buffer");
    using Result = track_sharing::FileIntegrityResult;
    assert(check.begin("device.kml", 750, expected) == Result::Pending);
    for (unsigned i = 0; i < 3; ++i)
    {
        assert(check.step(scratch, sizeof(scratch)) == Result::Pending && !io.opened);
    }
    assert(check.checkedBytes() == 750 && check.step(scratch, sizeof(scratch)) == Result::Ready && !check.active());
    expected[0] ^= 1;
    assert(check.begin("device.kml", 750, expected) == Result::Pending);
    for (unsigned i = 0; i < 3; ++i) check.step(scratch, sizeof(scratch));
    assert(check.step(scratch, sizeof(scratch)) == Result::HashMismatch);
    expected[0] ^= 1;
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        assert(check.begin("device.kml", 750, expected) == Result::Pending);
        io.short_read = mode == 0;
        io.close_fail = mode == 1;
        if (mode == 2) io.bytes.push_back(0);
        assert(check.step(scratch, sizeof(scratch)) == Result::StorageFailure && !io.opened);
        io.short_read = false;
        io.close_fail = false;
        if (mode == 2) io.bytes.pop_back();
    }
    assert(check.begin("device.kml", 750, expected) == Result::Pending && check.cancel() == Result::Cancelled && !check.active());
}
