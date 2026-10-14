#pragma once
#include "track_sharing/object_paths.h"
namespace track_sharing
{
struct PublicationSlotKey
{
    uint8_t object[32]{}, operation[16]{};
    ObjectFile slot = ObjectFile::PublicationA;
};
// Parse only canonical device paths. Caller owns output; no buffers or allocation.
// A candidate filename is not authentication of the journal or its receipt.
inline bool parsePublicationSlotPath(const char* path, size_t size, PublicationSlotKey& output)
{
    constexpr char prefix[] = "/trailmate/tracks/";
    constexpr size_t start = sizeof(prefix) - 1, slash = start + 64, key = slash + 1, suffix = key + 32;
    if (!path || size != suffix + 4 || std::memcmp(path, prefix, start) || path[slash] != '/') return false;
    const auto a = reinterpret_cast<uintptr_t>(path), b = reinterpret_cast<uintptr_t>(&output);
    if (a <= b ? b - a < size : a - b < sizeof(output)) return false;
    ObjectFile slot;
    if (!std::memcmp(path + suffix, ".pua", 4)) slot = ObjectFile::PublicationA;
    else if (!std::memcmp(path + suffix, ".pub", 4)) slot = ObjectFile::PublicationB;
    else return false;
    const auto nibble = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    unsigned object_bits = 0, operation_bits = 0;
    for (size_t i = 0; i < 96; ++i)
    {
        const int value = nibble(path[i < 64 ? start + i : key + i - 64]);
        if (value < 0) return false;
        if (i < 64) object_bits |= unsigned(value); else operation_bits |= unsigned(value);
    }
    if (!object_bits || !operation_bits) return false;
    for (size_t i = 0; i < 32; ++i) output.object[i] = uint8_t(nibble(path[start + 2*i]) * 16 + nibble(path[start + 2*i + 1]));
    for (size_t i = 0; i < 16; ++i) output.operation[i] = uint8_t(nibble(path[key + 2*i]) * 16 + nibble(path[key + 2*i + 1]));
    output.slot = slot;
    return true;
}
}
