#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/manifest_hash.h"
#include "track_sharing/reservation_candidate.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
int main(int argc, char** argv)
{
    const std::string hex = argc == 2 ? argv[1] : "9e01c44001010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101c410020202020202020202020202020202020001c000cf000000012a05f200cf000000012a05f200a174a094000000009400c0c00194c420030303030303030303030303030303030303030303030303030303030303030301d9246170706c69636174696f6e2f766e642e676f6f676c652d65617274682e6b6d6c2b786d6c01";
    std::vector<uint8_t> raw;
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        unsigned byte = 0;
        assert(std::sscanf(hex.substr(i, 2).c_str(), "%2x", &byte) == 1);
        raw.push_back(uint8_t(byte));
    }
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    track_sharing::ManifestView view;
    uint8_t id[32], revision[32];
    assert(!track_sharing::deriveManifestHashes({raw.data(), raw.size()}, view, hash, id, id));
    assert(track_sharing::deriveManifestHashes({raw.data(), raw.size()}, view, hash, id, revision));
    {
        track_sharing::RevisionReservation reserved;
        uint8_t author_hash[32], scratch[64];
        assert(hash.start() && hash.update(view.author.data, 64) && hash.finish(author_hash));
        reserved.operation[0] = 1;
        reserved.sequence = 1;
        reserved.generation = 7;
        reserved.revision = view.revision;
        reserved.phase = track_sharing::ReservationPhase::Checked;
        std::memcpy(reserved.object, id, 32);
        std::memcpy(reserved.candidate, revision, 32);
        std::memcpy(reserved.author, author_hash, 32);
        if (view.previous.size) std::memcpy(reserved.parent, view.previous.data, 32);
        const geocaching::ByteView source{raw.data(), raw.size()}, author = view.author;
        assert(track_sharing::reservationMatchesCandidate(source, author, 7, {author_hash, 32}, reserved, hash, view, scratch, sizeof(scratch)));
        reserved.candidate[31] ^= 1;
        assert(!track_sharing::reservationMatchesCandidate(source, author, 7, {author_hash, 32}, reserved, hash, view, scratch, sizeof(scratch)));
        reserved.candidate[31] ^= 1;
        assert(!track_sharing::reservationMatchesCandidate(source, author, 8, {author_hash, 32}, reserved, hash, view, scratch, sizeof(scratch)));
        reserved.phase = track_sharing::ReservationPhase::Signed;
        assert(!track_sharing::reservationMatchesCandidate(source, author, 7, {author_hash, 32}, reserved, hash, view, scratch, sizeof(scratch)));
    }
    for (auto byte : id) std::printf("%02x", byte);
    std::puts("");
    for (auto byte : revision) std::printf("%02x", byte);
    std::puts("");
    struct FailureHash
    {
        unsigned steps = 0, resets = 0;
        bool start()
        {
            ++steps;
            return true;
        }
        bool update(const uint8_t*, size_t) { return ++steps != 7; }
        bool finish(uint8_t* output)
        {
            ++steps;
            std::memset(output, 0x55, 32);
            return true;
        }
        void reset() { ++resets; }
    } failure;
    assert(!track_sharing::deriveManifestHashes({raw.data(), raw.size()}, view, failure, id, revision));
    assert(failure.resets == 1);
    for (unsigned i = 0; i < 32; ++i) assert(id[i] == 0 && revision[i] == 0);
}
