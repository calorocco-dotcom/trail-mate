#pragma once
#include <cstdint>
#include <cstring>
namespace track_sharing
{
enum class UploadPhase : uint8_t
{
    Receiving,
    Checking,
    Sealed,
    Failed
};
struct UploadJournal
{
    uint8_t id[16]{}, author_hash[32]{}, file_hash[32]{};
    uint64_t sequence = 0;
    uint32_t generation = 0, total = 0, durable_offset = 0;
    UploadPhase phase = UploadPhase::Receiving;
};
static_assert(sizeof(UploadJournal) <= 112, "Upload journal must not contain file storage");
constexpr size_t kUploadJournalBytes = 108;
inline bool uploadJournalValid(const UploadJournal& value)
{
    uint8_t id = 0, author = 0;
    for (auto byte : value.id) id |= byte;
    for (auto byte : value.author_hash) author |= byte;
    const auto phase = static_cast<uint8_t>(value.phase);
    return id && author && value.sequence && value.generation && value.total && value.total <= 8388608 && value.durable_offset <= value.total && phase <= 3 &&
           ((value.phase != UploadPhase::Checking && value.phase != UploadPhase::Sealed) || value.durable_offset == value.total);
}
inline uint16_t uploadJournalCrc(const uint8_t* bytes, size_t length)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= uint16_t(bytes[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit) crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
// TMKU/version1/id16/author-generationLE32/author-hash32/file-hash32/
// totalLE32/durable-offsetLE32/phase8/sequenceLE64/CRC16-CCITTLE16.
// CRC detects corruption, not authentication. Platform must check current
// device identity, admitted paths/media, and actual file hash before Sealed.
inline bool encodeUploadJournal(const UploadJournal& value, uint8_t* output, size_t capacity)
{
    if (!output || capacity < kUploadJournalBytes || !uploadJournalValid(value)) return false;
    std::memcpy(output, "TMKU", 4);
    output[4] = 1;
    std::memcpy(output + 5, value.id, 16);
    const auto u32 = [&](size_t offset, uint32_t number)
    {for(unsigned i=0;i<4;++i)output[offset+i]=uint8_t(number>>(i*8)); };
    u32(21, value.generation);
    std::memcpy(output + 25, value.author_hash, 32);
    std::memcpy(output + 57, value.file_hash, 32);
    u32(89, value.total);
    u32(93, value.durable_offset);
    output[97] = static_cast<uint8_t>(value.phase);
    for (unsigned i = 0; i < 8; ++i) output[98 + i] = uint8_t(value.sequence >> (i * 8));
    const auto crc = uploadJournalCrc(output, 106);
    output[106] = uint8_t(crc);
    output[107] = uint8_t(crc >> 8);
    return true;
}
inline bool decodeUploadJournal(const uint8_t* input, size_t size, UploadJournal& value)
{
    value = {};
    if (!input || size != kUploadJournalBytes || std::memcmp(input, "TMKU", 4) || input[4] != 1 || uploadJournalCrc(input, 106) != (uint16_t(input[106]) | (uint16_t(input[107]) << 8))) return false;
    const auto u32 = [&](size_t offset)
    {uint32_t number=0;for(unsigned i=0;i<4;++i)number|=uint32_t(input[offset+i])<<(i*8);return number; };
    std::memcpy(value.id, input + 5, 16);
    value.generation = u32(21);
    std::memcpy(value.author_hash, input + 25, 32);
    std::memcpy(value.file_hash, input + 57, 32);
    value.total = u32(89);
    value.durable_offset = u32(93);
    value.phase = static_cast<UploadPhase>(input[97]);
    for (unsigned i = 0; i < 8; ++i) value.sequence |= uint64_t(input[98 + i]) << (i * 8);
    return uploadJournalValid(value);
}
} // namespace track_sharing
