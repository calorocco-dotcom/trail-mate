#include "hostlink/track_snapshot.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace hostlink::management;
static uint8_t request_bytes[128], response[448];
int main()
{
    TrackSnapshotMetadata metadata;
    std::memset(metadata.id, 7, 16);
    std::memset(metadata.hash, 8, 32);
    metadata.bytes = 700;
    metadata.format = 2;
    metadata.sealed = true;
    auto request = [&](uint32_t offset, uint16_t maximum, bool hash)
    {
        geocaching::protocol::CmpWriter writer(request_bytes, sizeof(request_bytes));
        assert(writer.array(4) && writer.binary({metadata.id, 16}) && writer.unsignedInteger(offset) && writer.unsignedInteger(maximum));
        assert(hash ? writer.binary({metadata.hash, 32}) : writer.nil());
        return writer.size();
    };
    TrackSnapshotRequest decoded;
    size_t start = 0, length = 0, written = 0;
    auto bytes = request(0, 320, false);
    assert(decodeTrackSnapshotRequest({request_bytes, bytes}, decoded));
    assert(!decoded.has_hash && decoded.maximum == 320);
    assert(prepareTrackSnapshotResponse(decoded, metadata, response, sizeof(response), start, length, written));
    assert(length == 320 && written <= 400);
    std::memset(response + start, 0x55, length);
    geocaching::protocol::CmpReader reader({response, written});
    size_t fields = 0;
    uint64_t number = 0;
    geocaching::ByteView data;
    assert(reader.array(fields, 6) && fields == 6 && reader.binary(data, 16) && !std::memcmp(data.data, metadata.id, 16));
    assert(reader.unsignedInteger(number) && number == 700 && reader.binary(data, 32) && !std::memcmp(data.data, metadata.hash, 32));
    assert(reader.unsignedInteger(number) && number == 2 && reader.binary(data, 320) && data.size == 320 && data.data[319] == 0x55);
    assert(reader.unsignedInteger(number) && number == 0 && reader.finished());
    for (size_t i = 0; i < written; ++i) std::printf("%02x", response[i]);
    std::printf("\n");
    bytes = request(640, 320, true);
    assert(decodeTrackSnapshotRequest({request_bytes, bytes}, decoded));
    assert(prepareTrackSnapshotResponse(decoded, metadata, response, sizeof(response), start, length, written) && length == 60);
    decoded.expected_hash[0] ^= 1;
    assert(!prepareTrackSnapshotResponse(decoded, metadata, response, sizeof(response), start, length, written) && written == 0);
    decoded.expected_hash[0] ^= 1;
    metadata.sealed = false;
    assert(!prepareTrackSnapshotResponse(decoded, metadata, response, sizeof(response), start, length, written));
    metadata.sealed = true;
    assert(!prepareTrackSnapshotResponse(decoded, metadata, response, 10, start, length, written));
    bytes = request(1, 320, false);
    assert(!decodeTrackSnapshotRequest({request_bytes, bytes}, decoded));
    bytes = request(0, 0, false);
    assert(!decodeTrackSnapshotRequest({request_bytes, bytes}, decoded));
    bytes = request(0, 321, false);
    assert(!decodeTrackSnapshotRequest({request_bytes, bytes}, decoded));
    bytes = request(kTrackSnapshotMaxBytes, 320, true);
    assert(!decodeTrackSnapshotRequest({request_bytes, bytes}, decoded));
    bytes = request(0, 320, false);
    for (size_t n = 0; n < bytes; ++n) assert(!decodeTrackSnapshotRequest({request_bytes, n}, decoded));
    request_bytes[bytes] = 0;
    assert(!decodeTrackSnapshotRequest({request_bytes, bytes + 1}, decoded));
    return 0;
}
