#include "geocaching/protocol/cmp_reader.h"
#include "track_sharing/network_read.h"
#include <array>
#include <cassert>
#include <cstring>

int main()
{
    std::array<uint8_t, 16> id{};
    std::array<uint8_t, 32> object{}, revision{};
    std::array<uint8_t, 64> author{};
    std::array<uint8_t, 320> output{};
    id.fill(1);
    object.fill(2);
    revision.fill(3);
    author.fill(4);
    const geocaching::ByteView request{id.data(), id.size()}, target{object.data(), object.size()}, pin{revision.data(), revision.size()};
    size_t written = 0;
    auto header = [&](geocaching::protocol::CmpReader& reader, uint64_t operation, size_t fields)
    {
        size_t count = 0;
        uint64_t value = 0;
        geocaching::ByteView actual;
        assert(reader.array(count, 6) && count == 6);
        assert(reader.unsignedInteger(value) && value == 1);
        assert(reader.unsignedInteger(value) && value == 0);
        assert(reader.unsignedInteger(value) && value == operation);
        assert(reader.binary(actual, 16) && actual.size == 16 && !std::memcmp(actual.data, id.data(), 16));
        assert(reader.unsignedInteger(value) && value == 8192);
        assert(reader.array(count, fields) && count == fields);
    };
    assert(track_sharing::encodeTrackReadManifest(request, target, {}, 8192, output.data(), output.size(), written));
    {
        geocaching::protocol::CmpReader reader({output.data(), written});
        header(reader, 2, 2);
        geocaching::ByteView value;
        assert(reader.binary(value, 32) && value.size == 32);
        assert(reader.nil() && reader.finished());
    }
    assert(track_sharing::encodeTrackReadManifest(request, target, pin, 8192, output.data(), output.size(), written));
    assert(!track_sharing::encodeTrackReadManifest(request, target, pin, 8192, output.data(), 20, written) && !written);
    assert(!track_sharing::encodeTrackReadManifest(request, {output.data(), 32}, {}, 8192, output.data(), output.size(), written));
    assert(track_sharing::encodeTrackReadFile(request, target, 8388607, 1, 8192, output.data(), output.size(), written));
    assert(!track_sharing::encodeTrackReadFile(request, target, 8388607, 2, 8192, output.data(), output.size(), written));
    assert(!track_sharing::encodeTrackReadFile(request, target, 0, 1025, 8192, output.data(), output.size(), written));
    track_sharing::ReadQuery query;
    query.west = 1700000000;
    query.east = -1700000000;
    query.south = -10000000;
    query.north = 10000000;
    query.author = {author.data(), author.size()};
    query.kind = 1;
    query.state = -1;
    query.has_snapshot = query.has_cursor = true;
    query.snapshot = 9007199254740993ULL;
    query.cursor_object = target;
    query.cursor_binding = pin;
    assert(track_sharing::encodeTrackReadQuery(request, query, 8192, output.data(), output.size(), written));
    {
        geocaching::protocol::CmpReader reader({output.data(), written});
        header(reader, 1, 7);
        size_t count;
        int64_t value;
        uint64_t number;
        geocaching::ByteView binary;
        assert(reader.array(count, 4) && count == 4);
        assert(reader.signedInteger(value) && value == query.west);
        assert(reader.signedInteger(value) && value == query.south);
        assert(reader.signedInteger(value) && value == query.east);
        assert(reader.signedInteger(value) && value == query.north);
        assert(reader.unsignedInteger(number) && number == 1);
        assert(reader.binary(binary, 64) && binary.size == 64);
        assert(reader.nil());
        assert(reader.unsignedInteger(number) && number == 16);
        assert(reader.unsignedInteger(number) && number == query.snapshot);
        assert(reader.array(count, 2) && count == 2);
        assert(reader.binary(binary, 32) && binary.size == 32);
        assert(reader.binary(binary, 32) && binary.size == 32);
        assert(reader.finished());
    }
    query.has_snapshot = false;
    assert(!track_sharing::encodeTrackReadQuery(request, query, 8192, output.data(), output.size(), written));
    query.has_cursor = false;
    query.limit = 33;
    assert(!track_sharing::encodeTrackReadQuery(request, query, 8192, output.data(), output.size(), written));
    query.limit = 16;
    query.west = 1800000000;
    assert(!track_sharing::encodeTrackReadQuery(request, query, 8192, output.data(), output.size(), written));
    query.west = 0;
    id.fill(0);
    assert(!track_sharing::encodeTrackReadQuery(request, query, 8192, output.data(), output.size(), written));
}
