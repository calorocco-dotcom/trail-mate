#pragma once
#include "track_sharing/publication_journal.h"

namespace track_sharing
{
// Copied from an authenticated announcement plus matching GET_CAPABILITIES.
// Persistence preserves that admission evidence; CRC is not authentication.
struct PublicationDirectory
{
    uint8_t delivery[16]{}, public_key[64]{}, epoch[16]{};
    uint32_t file_limit = 0;
    uint16_t chunk_limit = 0;
    uint8_t query_limit = 0;
};
static_assert(sizeof(PublicationDirectory) <= 104, "Directory binding owns metadata only");
constexpr size_t kPublicationDirectoryBytes = 110;

inline bool publicationDirectoryValid(const PublicationDirectory& value)
{
    for (const auto field : {geocaching::ByteView{value.delivery, 16}, {value.public_key, 64}, {value.epoch, 16}})
    {
        uint8_t bits = 0;
        for (size_t i = 0; i < field.size; ++i) bits |= field.data[i];
        if (!bits) return false;
    }
    return value.file_limit && value.file_limit <= 8388608 && value.chunk_limit && value.chunk_limit <= 1024 &&
           value.query_limit && value.query_limit <= 32;
}
inline bool publicationDirectoryMatches(const PublicationDirectory& value, const PublicationJournal& job)
{
    return publicationDirectoryValid(value) && publicationJournalValid(job) && job.file_bytes <= value.file_limit &&
           !std::memcmp(value.delivery, job.directory, 16) && !std::memcmp(value.epoch, job.directory_epoch, 16);
}
inline bool publicationDirectoriesEqual(const PublicationDirectory& a, const PublicationDirectory& b)
{
    return !std::memcmp(a.delivery, b.delivery, 16) && !std::memcmp(a.public_key, b.public_key, 64) &&
           !std::memcmp(a.epoch, b.epoch, 16) && a.file_limit == b.file_limit &&
           a.chunk_limit == b.chunk_limit && a.query_limit == b.query_limit;
}
inline bool encodePublicationDirectory(const PublicationDirectory& value, uint8_t* output, size_t capacity)
{
    if (!output || capacity < kPublicationDirectoryBytes || !publicationDirectoryValid(value) ||
        signingSpansOverlap(output, kPublicationDirectoryBytes, reinterpret_cast<const uint8_t*>(&value), sizeof(value))) return false;
    std::memcpy(output, "TMTD", 4);
    output[4] = 1;
    std::memcpy(output + 5, value.delivery, 16);
    std::memcpy(output + 21, value.public_key, 64);
    std::memcpy(output + 85, value.epoch, 16);
    for (unsigned i = 0; i < 4; ++i) output[101 + i] = uint8_t(value.file_limit >> (8 * i));
    output[105] = uint8_t(value.chunk_limit);
    output[106] = uint8_t(value.chunk_limit >> 8);
    output[107] = value.query_limit;
    const auto crc = uploadJournalCrc(output, 108);
    output[108] = uint8_t(crc);
    output[109] = uint8_t(crc >> 8);
    return true;
}
inline bool decodePublicationDirectory(const uint8_t* input, size_t size, PublicationDirectory& output)
{
    if (!input || size != kPublicationDirectoryBytes ||
        signingSpansOverlap(input, size, reinterpret_cast<const uint8_t*>(&output), sizeof(output)) ||
        std::memcmp(input, "TMTD", 4) || input[4] != 1 ||
        uploadJournalCrc(input, 108) != (uint16_t(input[108]) | (uint16_t(input[109]) << 8))) return false;
    std::memcpy(output.delivery, input + 5, 16);
    std::memcpy(output.public_key, input + 21, 64);
    std::memcpy(output.epoch, input + 85, 16);
    output.file_limit = 0;
    for (unsigned i = 0; i < 4; ++i) output.file_limit |= uint32_t(input[101 + i]) << (8 * i);
    output.chunk_limit = uint16_t(input[105]) | (uint16_t(input[106]) << 8);
    output.query_limit = input[107];
    if (publicationDirectoryValid(output)) return true;
    output = {};
    return false;
}
} // namespace track_sharing
