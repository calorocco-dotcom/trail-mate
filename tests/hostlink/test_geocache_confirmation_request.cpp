#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/geocache_confirmation_request.h"
#include <array>
#include <cassert>
int main()
{
    using namespace hostlink::management;
    std::array<uint8_t, 448> input{};
    std::array<uint8_t, 192> proof{};
    std::array<uint8_t, 64> key{}, signature{};
    std::array<uint8_t, 32> hash{};
    std::array<uint8_t, 16> transfer{}, epoch{};
    transfer.fill(1);
    const auto encode = [&](uint64_t generation, size_t proof_size, bool tail = false)
    {
        geocaching::protocol::CmpWriter writer(input.data(), input.size());
        assert(writer.array(7) && writer.binary({transfer.data(), 16}) && writer.binary({hash.data(), 32}) && writer.unsignedInteger(generation) &&
               writer.binary({proof.data(), proof_size}) && writer.binary({signature.data(), 64}) && writer.binary({key.data(), 64}) && writer.binary({epoch.data(), 16}));
        if (tail) assert(writer.nil());
        return writer.size();
    };
    GeocacheConfirmationRequest parsed;
    auto size = encode(UINT32_MAX, 192);
    assert(size <= 448 && parseGeocacheConfirmationRequest(input.data(), size, parsed) == Status::Ok && parsed.author_generation == UINT32_MAX);
    assert(parsed.proof.data >= input.data() && parsed.proof.data < input.data() + size);
    assert(parseGeocacheConfirmationRequest(input.data(), size - 1, parsed) == Status::InvalidField && !parsed.proof.data);
    size = encode(0, 1);
    assert(parseGeocacheConfirmationRequest(input.data(), size, parsed) == Status::InvalidField);
    size = encode(uint64_t(UINT32_MAX) + 1, 1);
    assert(parseGeocacheConfirmationRequest(input.data(), size, parsed) == Status::InvalidField);
    size = encode(1, 0);
    assert(parseGeocacheConfirmationRequest(input.data(), size, parsed) == Status::InvalidField);
    size = encode(1, 1, true);
    assert(parseGeocacheConfirmationRequest(input.data(), size, parsed) == Status::InvalidField);
    transfer.fill(0);
    size = encode(1, 1);
    assert(parseGeocacheConfirmationRequest(input.data(), size, parsed) == Status::InvalidField);
    parsed.author_generation = 7;
    assert(parseGeocacheConfirmationRequest(reinterpret_cast<const uint8_t*>(&parsed), sizeof(parsed), parsed) == Status::InvalidField && parsed.author_generation == 7);
}
