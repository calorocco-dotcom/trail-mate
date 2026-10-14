#include "track_sharing/read_query_response.h"
#include <array>
#include <cassert>

static track_sharing::ReadQuery query;
static track_sharing::ReadQueryRow row;
static track_sharing::ReadQueryResponse result;
static std::array<uint8_t, 2048> raw;
int main()
{
    std::array<uint8_t, 16> id{};
    std::array<uint8_t, 32> first{}, second{}, binding{};
    std::array<uint8_t, 64> author{};
    id.fill(1);
    first.fill(2);
    second.fill(3);
    binding.fill(4);
    author.fill(5);
    query.west = 1700000000;
    query.east = -1700000000;
    query.south = -100000000;
    query.north = 100000000;
    query.limit = 2;
    const geocaching::ByteView request{id.data(), id.size()}, digest{binding.data(), binding.size()};
    auto encode = [&](bool reverse, bool wrong_cursor, uint64_t snapshot, int32_t west)
    {
        geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
        assert(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.binary(request) && writer.unsignedInteger(200) && writer.array(3) && writer.unsignedInteger(snapshot) && writer.array(2));
        assert(writer.binary({second.data(), second.size()}) && writer.binary(wrong_cursor ? geocaching::ByteView{first.data(), first.size()} : digest) && writer.array(2));
        for (unsigned i = 0; i < 2; i++)
        {
            const auto& object = (i == 0) != reverse ? first : second;
            assert(writer.array(10) && writer.binary({object.data(), object.size()}) && writer.binary({second.data(), second.size()}) && writer.unsignedInteger(1) && writer.unsignedInteger(0) && writer.unsignedInteger(0) && writer.text("Trail") && writer.array(4));
            assert(writer.signedInteger(west) && writer.signedInteger(0) && writer.signedInteger(-1750000000) && writer.signedInteger(10000000) && writer.array(4));
            assert(writer.unsignedInteger(1) && writer.nil() && writer.nil() && writer.unsignedInteger(2) && writer.unsignedInteger(100) && writer.binary({author.data(), author.size()}));
        }
        return writer.size();
    };
    size_t length = encode(false, false, 9007199254740993ULL, 1750000000);
    assert(track_sharing::decodeTrackReadQueryResponse({raw.data(), length}, request, query, digest, row, result) && result.rows == 2 && result.has_cursor && result.snapshot == 9007199254740993ULL);
    raw[length] = 0;
    assert(!track_sharing::decodeTrackReadQueryResponse({raw.data(), length + 1}, request, query, digest, row, result));
    length = encode(true, false, 1, 1750000000);
    assert(!track_sharing::decodeTrackReadQueryResponse({raw.data(), length}, request, query, digest, row, result));
    length = encode(false, true, 1, 1750000000);
    assert(!track_sharing::decodeTrackReadQueryResponse({raw.data(), length}, request, query, digest, row, result));
    length = encode(false, false, 1, 1750000000);
    query.kind = 1;
    assert(!track_sharing::decodeTrackReadQueryResponse({raw.data(), length}, request, query, digest, row, result));
    query.kind = -1;
    query.has_snapshot = true;
    query.snapshot = 2;
    assert(!track_sharing::decodeTrackReadQueryResponse({raw.data(), length}, request, query, digest, row, result));
    query.has_snapshot = false;
    query.west = 0;
    query.east = 10000000;
    assert(!track_sharing::decodeTrackReadQueryResponse({raw.data(), length}, request, query, digest, row, result));
}
