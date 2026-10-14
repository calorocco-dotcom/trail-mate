#include "geocaching/protocol/cmp_reader.h"
#include "track_sharing/network_request.h"
#include <array>
#include <cassert>
#include <cstring>

int main()
{
    std::array<uint8_t, 16> id{}, token{};
    std::array<uint8_t, 32> hash{};
    std::array<uint8_t, 1024> chunk{};
    std::array<uint8_t, 1200> output{};
    id.fill(1);
    token.fill(2);
    hash.fill(3);
    chunk.fill(4);
    const geocaching::ByteView request{id.data(), id.size()}, upload{token.data(), token.size()};
    size_t written = 0;
    assert(track_sharing::encodeTrackUploadBegin(request, {hash.data(), hash.size()}, 8388608, 8192, output.data(), output.size(), written));
    auto checkHeader = [&](geocaching::protocol::CmpReader& reader, unsigned operation, size_t fields)
    {
        size_t count = 0;
        uint64_t value = 0;
        geocaching::ByteView decoded;
        assert(reader.array(count, 6) && count == 6);
        assert(reader.unsignedInteger(value) && value == 1);
        assert(reader.unsignedInteger(value) && value == 0);
        assert(reader.unsignedInteger(value) && value == operation);
        assert(reader.binary(decoded, 16) && decoded.size == 16 && !std::memcmp(decoded.data, id.data(), 16));
        assert(reader.unsignedInteger(value) && value == 8192);
        assert(reader.array(count, 3) && count == fields);
    };
    geocaching::protocol::CmpReader begin({output.data(), written});
    checkHeader(begin, 4, 2);
    geocaching::ByteView decoded;
    uint64_t value = 0;
    assert(begin.binary(decoded, 32) && decoded.size == 32 && !std::memcmp(decoded.data, hash.data(), 32));
    assert(begin.unsignedInteger(value) && value == 8388608 && begin.finished());
    assert(track_sharing::encodeTrackUploadChunk(request, upload, 8387584, {chunk.data(), chunk.size()}, 8192, output.data(), output.size(), written));
    geocaching::protocol::CmpReader block({output.data(), written});
    checkHeader(block, 5, 3);
    assert(block.binary(decoded, 16) && decoded.size == 16 && !std::memcmp(decoded.data, token.data(), 16));
    assert(block.unsignedInteger(value) && value == 8387584);
    assert(block.binary(decoded, 1024) && decoded.size == 1024 && !std::memcmp(decoded.data, chunk.data(), 1024) && block.finished());
    assert(track_sharing::encodeTrackUploadFinish(request, upload, 8192, output.data(), output.size(), written));
    geocaching::protocol::CmpReader finish({output.data(), written});
    checkHeader(finish, 6, 1);
    assert(finish.binary(decoded, 16) && decoded.size == 16 && finish.finished());
    assert(!track_sharing::encodeTrackUploadChunk(request, upload, 8387585, {chunk.data(), chunk.size()}, 8192, output.data(), output.size(), written) && written == 0);
    assert(!track_sharing::encodeTrackUploadChunk(request, upload, 0, {chunk.data(), 0}, 8192, output.data(), output.size(), written));
    assert(!track_sharing::encodeTrackUploadChunk(request, upload, 0, {chunk.data(), chunk.size()}, 8192, output.data(), 32, written) && written == 0);
    assert(!track_sharing::encodeTrackUploadFinish(request, upload, 511, output.data(), output.size(), written));
    assert(!track_sharing::encodeTrackUploadFinish({output.data(), 16}, upload, 8192, output.data(), output.size(), written));
    assert(!track_sharing::encodeTrackUploadBegin(request, {hash.data(), hash.size()}, 8388609, 8192, output.data(), output.size(), written));
    id.fill(0);
    assert(!track_sharing::encodeTrackUploadFinish(request, upload, 8192, output.data(), output.size(), written));
}
