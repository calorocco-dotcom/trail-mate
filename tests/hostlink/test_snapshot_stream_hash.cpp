#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include <cassert>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
// Older MinGW headers lack SecureZeroMemory. Keep the test-only platform
// hook explicit without changing the SDK or its actual SHA implementation.
extern "C" void mbedtls_platform_zeroize(void* buffer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(buffer);
    while (length--) *bytes++ = 0;
}
#endif

static mbedtls_sha256_context context;
static uint8_t scratch[320], output[32];
int main()
{
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    assert(!hash.finish(output));
    assert(hash.start());
    assert(hash.update(reinterpret_cast<const uint8_t*>("a"), 1));
    assert(hash.update(reinterpret_cast<const uint8_t*>("bc"), 2));
    assert(hash.finish(output));
    const uint8_t expected[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    assert(!std::memcmp(output, expected, 32));
    assert(!hash.finish(output));
    assert(hash.start());
    assert(!hash.update(nullptr, 1));
    assert(!hash.finish(output));
    assert(hash.start());
    std::memset(scratch, 'a', sizeof(scratch));
    for (unsigned copied = 0; copied < 1000000;)
    {
        const auto length = std::min<unsigned>(sizeof(scratch), 1000000 - copied);
        assert(hash.update(scratch, length));
        copied += length;
    }
    assert(hash.finish(output));
    for (auto byte : output) std::printf("%02x", byte);
    std::printf("\n");
    return 0;
}
