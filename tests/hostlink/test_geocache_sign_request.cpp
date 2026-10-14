#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/geocache_sign_request.h"
#include <array>
#include <cassert>
int main()
{
    using namespace hostlink::management;
    std::array<uint8_t, 16> transfer{}, operation{};
    transfer.fill(1);
    operation.fill(2);
    std::array<uint8_t, 32> hash{};
    std::array<uint8_t, 448> encoded{};
    const auto make = [&](uint64_t size, uint64_t generation, bool tail = false)
    {
        geocaching::protocol::CmpWriter writer(encoded.data(), encoded.size());
        assert(writer.array(5) && writer.binary({transfer.data(), transfer.size()}) && writer.binary({hash.data(), hash.size()}) &&
               writer.unsignedInteger(size) && writer.unsignedInteger(generation) && writer.binary({operation.data(), operation.size()}));
        if (tail) assert(writer.nil());
        return writer.size();
    };
    GeocacheSignRequest parsed;
    auto size = make(4096, UINT32_MAX);
    assert(parseGeocacheSignRequest(encoded.data(), size, parsed) == Status::Ok && parsed.bytes == 4096 && parsed.author_generation == UINT32_MAX);
    assert(parsed.transfer.data >= encoded.data() && parsed.transfer.data < encoded.data() + size);
    {
        geocaching::protocol::CmpWriter writer(encoded.data(), encoded.size());
        assert(writer.array(7) && writer.binary({transfer.data(), 16}) && writer.binary({hash.data(), 32}) &&
               writer.unsignedInteger(100) && writer.unsignedInteger(7) && writer.binary({operation.data(), 16}) &&
               writer.binary({transfer.data(), 16}) && writer.unsignedInteger(UINT64_MAX));
        assert(parseGeocacheSignRequest(encoded.data(), writer.size(), parsed) == Status::Ok &&
               parsed.draft.size == 16 && parsed.draft_generation == UINT64_MAX);
        size = make(4096, UINT32_MAX);
    }
    assert(parseGeocacheSignRequest(encoded.data(), size - 1, parsed) == Status::InvalidField && !parsed.transfer.data);
    for (auto value : {uint64_t(0), uint64_t(4097), UINT64_MAX})
    {
        size = make(value, 1);
        assert(parseGeocacheSignRequest(encoded.data(), size, parsed) == Status::InvalidField);
    }
    size = make(1, uint64_t(UINT32_MAX) + 1);
    assert(parseGeocacheSignRequest(encoded.data(), size, parsed) == Status::InvalidField);
    size = make(1, 0);
    assert(parseGeocacheSignRequest(encoded.data(), size, parsed) == Status::InvalidField);
    size = make(1, 1, true);
    assert(parseGeocacheSignRequest(encoded.data(), size, parsed) == Status::InvalidField);
    operation.fill(0);
    size = make(1, 1);
    assert(parseGeocacheSignRequest(encoded.data(), size, parsed) == Status::InvalidField);
    assert(parseGeocacheSignRequest(nullptr, 0, parsed) == Status::InvalidField);
    parsed.bytes = 5;
    assert(parseGeocacheSignRequest(reinterpret_cast<const uint8_t*>(&parsed), sizeof(parsed), parsed) == Status::InvalidField && parsed.bytes == 5);
}
