#pragma once
#include "geocaching/domain/record.h"
#include "track_sharing/upload_journal.h"

namespace track_sharing
{
constexpr size_t kImageSourceBindingBytes = 75;
constexpr uint32_t kImageSourceByteLimit = 8U * 1024U * 1024U;
struct ImageSourceBindingView
{
    geocaching::ByteView source_hash, file_hash;
    uint32_t file_bytes = 0;
};
static_assert(sizeof(ImageSourceBindingView) <= 48, "Image binding views must not copy digests or file data");
inline bool imageBindingDisjoint(const void* first, size_t first_bytes, const void* second, size_t second_bytes)
{
    const auto a = reinterpret_cast<uintptr_t>(first), b = reinterpret_cast<uintptr_t>(second);
    return a <= b ? b - a >= first_bytes : a - b >= second_bytes;
}
// TMSI/version1/fileBytesLE32/referenceSHA256/fileSHA256/CRC16-CCITTLE16.
// A reference hash binds the exact decoded KML reference, not a normalized URL.
// CRC is corruption detection. This record alone proves neither a file's hash
// nor an author's signature; callers must establish those independently.
inline bool encodeImageSourceBinding(const ImageSourceBindingView& value, uint8_t* output, size_t capacity)
{
    if (!output || capacity < kImageSourceBindingBytes || !value.source_hash.data || value.source_hash.size != 32 ||
        !value.file_hash.data || value.file_hash.size != 32 || !value.file_bytes || value.file_bytes > kImageSourceByteLimit ||
        !imageBindingDisjoint(output, kImageSourceBindingBytes, &value, sizeof(value)) ||
        !imageBindingDisjoint(output, kImageSourceBindingBytes, value.source_hash.data, 32) ||
        !imageBindingDisjoint(output, kImageSourceBindingBytes, value.file_hash.data, 32)) return false;
    std::memcpy(output, "TMSI", 4);
    output[4] = 1;
    for (unsigned i = 0; i < 4; ++i) output[5 + i] = uint8_t(value.file_bytes >> (i * 8));
    std::memcpy(output + 9, value.source_hash.data, 32);
    std::memcpy(output + 41, value.file_hash.data, 32);
    const auto crc = uploadJournalCrc(output, 73);
    output[73] = uint8_t(crc);
    output[74] = uint8_t(crc >> 8);
    return true;
}
inline bool decodeImageSourceBinding(const uint8_t* input, size_t size, ImageSourceBindingView& output)
{
    if (!imageBindingDisjoint(input, size, &output, sizeof(output))) return false;
    output = {};
    if (!input || size != kImageSourceBindingBytes || std::memcmp(input, "TMSI", 4) || input[4] != 1 ||
        uploadJournalCrc(input, 73) != (uint16_t(input[73]) | uint16_t(input[74]) << 8)) return false;
    uint32_t bytes = 0;
    for (unsigned i = 0; i < 4; ++i) bytes |= uint32_t(input[5 + i]) << (i * 8);
    if (!bytes || bytes > kImageSourceByteLimit) return false;
    output = {{input + 9, 32}, {input + 41, 32}, bytes};
    return true;
}
inline bool imageSourceBindingMatches(const ImageSourceBindingView& value, const ImageSourceBindingView& expected)
{
    return value.source_hash.data && value.source_hash.size == 32 && value.file_hash.data && value.file_hash.size == 32 &&
           expected.source_hash.data && expected.source_hash.size == 32 && expected.file_hash.data && expected.file_hash.size == 32 &&
           value.file_bytes == expected.file_bytes && !std::memcmp(value.source_hash.data, expected.source_hash.data, 32) &&
           !std::memcmp(value.file_hash.data, expected.file_hash.data, 32);
}
} // namespace track_sharing
