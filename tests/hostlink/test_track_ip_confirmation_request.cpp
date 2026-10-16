#include "geocaching/protocol/cmp_writer.h"
#include "track_sharing/ip_confirmation_request.h"
#include <array>
#include <cassert>
int main()
{
    std::array<uint8_t, 320> wire{};
    std::array<uint8_t, 64> key{};
    key.fill(1);
    std::array<uint8_t, 32> hash{};
    hash.fill(2);
    std::array<uint8_t, 16> id{};
    id.fill(3);
    const auto encode = [&](uint64_t generation, uint64_t revision, size_t id_size = 16, bool tail = false)
    {
        geocaching::protocol::CmpWriter writer(wire.data(), wire.size());
        assert(writer.array(11) && writer.binary({id.data(), id_size}) && writer.binary({id.data(), 16}) &&
               writer.binary({hash.data(), 32}) && writer.unsignedInteger(generation) && writer.binary({key.data(), 64}) &&
               writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) &&
               writer.binary({hash.data(), 32}) && writer.binary({hash.data(), 32}) && writer.unsignedInteger(revision));
        if (tail) assert(writer.nil());
        return writer.size();
    };
    track_sharing::IpConfirmationRequest parsed;
    auto size = encode(UINT32_MAX, UINT32_MAX);
    assert(size <= 288 && track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
    assert(parsed.generation == UINT32_MAX && parsed.revision == UINT32_MAX);
    assert(parsed.operation.data >= wire.data() && parsed.operation.data < wire.data() + size);
    for (size_t truncated = 0; truncated < size; ++truncated)
    {
        assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), truncated}, parsed));
        assert(!parsed.operation.data && parsed.generation == 0);
    }
    for (const auto value : {uint64_t(0), uint64_t(UINT32_MAX) + 1})
    {
        size = encode(value, 1);
        assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
        size = encode(1, value);
        assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
    }
    size = encode(1, 1, 15);
    assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
    size = encode(1, 1, 16, true);
    assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
    id.fill(0);
    size = encode(1, 1);
    assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
    id.fill(3);
    hash.fill(0);
    size = encode(1, 1);
    assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
    hash.fill(2);
    key.fill(0);
    size = encode(1, 1);
    assert(!track_sharing::decodeIpConfirmationRequest({wire.data(), size}, parsed));
    parsed.generation = 7;
    assert(!track_sharing::decodeIpConfirmationRequest({reinterpret_cast<const uint8_t*>(&parsed), sizeof(parsed)}, parsed));
    assert(parsed.generation == 7);
    {
        key.fill(1);
        id.fill(3);
        hash.fill(2);
        std::array<uint8_t, 416> imported;
        geocaching::protocol::CmpWriter writer(imported.data(), imported.size());
        assert(writer.array(14) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) && writer.binary({hash.data(), 32}) &&
               writer.unsignedInteger(UINT32_MAX) && writer.binary({key.data(), 64}) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) && writer.binary({id.data(), 16}) &&
               writer.binary({hash.data(), 32}) && writer.binary({hash.data(), 32}) && writer.unsignedInteger(UINT32_MAX) &&
               writer.binary({id.data(), 16}) && writer.binary({hash.data(), 32}) && writer.binary({key.data(), 64}));
        assert(writer.size() <= 416 && !track_sharing::decodeIpConfirmationRequest({imported.data(), writer.size()}, parsed));
        assert(track_sharing::decodeIpConfirmationRequest({imported.data(), writer.size()}, parsed, true) && parsed.manifest_signature.size == 64);
        for (size_t truncated = 0; truncated < writer.size(); ++truncated) assert(!track_sharing::decodeIpConfirmationRequest({imported.data(), truncated}, parsed, true));
    }
}
