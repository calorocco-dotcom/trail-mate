#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/read_query_binding.h"
#include <array>
#include <cassert>
#include <cstring>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
static mbedtls_sha256_context context;
static track_sharing::ReadQuery query;
static std::array<uint8_t, 128> workspace;
static std::array<uint8_t, 32> output, previous;
int main()
{
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    query.west = 1700000000;
    query.east = -1700000000;
    query.south = -100000000;
    query.north = 100000000;
    assert(track_sharing::deriveReadQueryBinding(query, hash, workspace.data(), workspace.size(), output.data()));
    const uint8_t expected[] = {0x53, 0xc8, 0x26, 0xb0, 0x11, 0x1d, 0xfb, 0xb2, 0x13, 0x6d, 0xf1, 0x5a, 0xe5, 0xc7, 0xda, 0x76, 0xba, 0x7d, 0x93, 0xf2, 0x23, 0x81, 0xb3, 0xc3, 0xd5, 0xa7, 0x3a, 0x64, 0x22, 0x3a, 0x3a, 0x09};
    assert(!std::memcmp(output.data(), expected, 32));
    previous = output;
    query.has_snapshot = true;
    query.snapshot = 9007199254740993ULL;
    assert(track_sharing::deriveReadQueryBinding(query, hash, workspace.data(), workspace.size(), output.data()) && output == previous);
    query.state = 1;
    assert(track_sharing::deriveReadQueryBinding(query, hash, workspace.data(), workspace.size(), output.data()) && output != previous);
    query.state = 0;
    assert(!track_sharing::deriveReadQueryBinding(query, hash, workspace.data(), 1, output.data()));
    assert(!track_sharing::deriveReadQueryBinding(query, hash, workspace.data(), workspace.size(), workspace.data()));
    query.cursor_binding = {output.data(), output.size()};
    assert(!track_sharing::deriveReadQueryBinding(query, hash, workspace.data(), workspace.size(), output.data()));
    query.cursor_binding = {};
    query.west = 1800000000;
    assert(!track_sharing::deriveReadQueryBinding(query, hash, workspace.data(), workspace.size(), output.data()));
}
