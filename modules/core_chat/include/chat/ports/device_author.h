#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat
{
enum class DeviceAuthorResult : uint8_t
{
    Ok,
    Unsupported,
    Unavailable,
    StaleIdentity,
    InvalidInput,
    StorageError,
};

// Fixed domain, canonical CMP1 array [1, session16, challenge32, public64,
// generation]. All inputs must lie outside the caller-owned output workspace.
inline bool deviceAuthorProofMessage(const uint8_t* session, const uint8_t* challenge,
                                     const uint8_t* public_key, uint32_t generation,
                                     uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    constexpr char domain[] = "trailmate.hostlink.author/v1\0";
    const size_t integer_bytes = generation <= 127 ? 1 : generation <= 255 ? 2
                                                     : generation <= 65535 ? 3
                                                                           : 5;
    const size_t required = sizeof(domain) - 1 + 1 + 1 + 18 + 34 + 66 + integer_bytes;
    if (!session || !challenge || !public_key || !generation || !output || capacity < required) return false;
    const auto start = reinterpret_cast<uintptr_t>(output);
    const auto overlaps = [start, required](const uint8_t* source, size_t length)
    {
        const auto input = reinterpret_cast<uintptr_t>(source);
        return input < start + required && start < input + length;
    };
    if (overlaps(session, 16) || overlaps(challenge, 32) || overlaps(public_key, 64)) return false;
    std::memcpy(output, domain, sizeof(domain) - 1);
    size_t offset = sizeof(domain) - 1;
    output[offset++] = 0x95;
    output[offset++] = 1;
    for (unsigned field = 0; field < 3; ++field)
    {
        const size_t length = field == 0 ? 16 : field == 1 ? 32
                                                           : 64;
        const auto* source = field == 0 ? session : field == 1 ? challenge
                                                               : public_key;
        output[offset++] = 0xc4;
        output[offset++] = static_cast<uint8_t>(length);
        std::memcpy(output + offset, source, length);
        offset += length;
    }
    if (integer_bytes == 1) output[offset++] = static_cast<uint8_t>(generation);
    else
    {
        output[offset++] = integer_bytes == 2 ? 0xcc : integer_bytes == 3 ? 0xcd
                                                                          : 0xce;
        for (size_t remaining = integer_bytes - 1; remaining; --remaining)
            output[offset++] = static_cast<uint8_t>(generation >> ((remaining - 1) * 8));
    }
    written = offset;
    return true;
}
} // namespace chat
