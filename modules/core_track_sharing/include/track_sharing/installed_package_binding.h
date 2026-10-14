#pragma once
#include "track_sharing/manifest_extensions.h"
#include "track_sharing/sign_manifest.h"
#include "track_sharing/upload_journal.h"
namespace track_sharing
{
struct InstalledPackageBinding
{
    uint8_t object[32]{}, revision[32]{}, kml_hash[32]{}, index_hash[32]{};
    uint64_t availability = 0;
    uint32_t kml_bytes = 0, index_bytes = 0;
    uint8_t count = 0;
};
constexpr size_t kInstalledPackageBindingBytes = 154;
inline bool installedPackageBindingValid(const InstalledPackageBinding& value)
{
    uint8_t object = 0, revision = 0, index = 0;
    for (size_t i = 0; i < 32; ++i)
    {
        object |= value.object[i];
        revision |= value.revision[i];
        index |= value.index_hash[i];
    }
    if (!object || !revision || !value.kml_bytes || value.kml_bytes > 8388608 || value.count > 64) return false;
    if (!value.count) return !value.index_bytes && !index && !value.availability;
    return value.index_bytes && value.index_bytes <= kMaxAttachmentIndexBytes && (value.count == 64 || value.availability < (uint64_t(1) << value.count));
}
// CRC catches torn/corrupt local records; it does NOT authenticate the author.
// Loaders must verify cached original signature/object/revision/index and bytes.
inline bool encodeInstalledPackageBinding(const InstalledPackageBinding& value, uint8_t* output, size_t capacity)
{
    if (!output || capacity < kInstalledPackageBindingBytes || !installedPackageBindingValid(value) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&value), sizeof(value), output, kInstalledPackageBindingBytes)) return false;
    std::memcpy(output, "TMPK", 4);
    output[4] = 1;
    output[5] = value.count ? 1 : 0;
    output[6] = value.count;
    output[7] = 0;
    std::memcpy(output + 8, value.object, 32);
    std::memcpy(output + 40, value.revision, 32);
    std::memcpy(output + 72, value.kml_hash, 32);
    std::memcpy(output + 104, value.index_hash, 32);
    for (unsigned i = 0; i < 4; ++i)
    {
        output[136 + i] = uint8_t(value.kml_bytes >> (8 * i));
        output[140 + i] = uint8_t(value.index_bytes >> (8 * i));
    }
    for (unsigned i = 0; i < 8; ++i) output[144 + i] = uint8_t(value.availability >> (8 * i));
    const auto crc = uploadJournalCrc(output, 152);
    output[152] = uint8_t(crc);
    output[153] = uint8_t(crc >> 8);
    return true;
}
inline bool decodeInstalledPackageBinding(const uint8_t* input, size_t size, InstalledPackageBinding& output)
{
    if (signingSpansOverlap(input, size, reinterpret_cast<const uint8_t*>(&output), sizeof(output))) return false;
    output = {};
    if (!input || size != kInstalledPackageBindingBytes || std::memcmp(input, "TMPK", 4) || input[4] != 1 || input[5] > 1 || input[7] ||
        uploadJournalCrc(input, 152) != (uint16_t(input[152]) | (uint16_t(input[153]) << 8))) return false;
    output.count = input[6];
    if ((output.count != 0) != (input[5] != 0)) return false;
    std::memcpy(output.object, input + 8, 32);
    std::memcpy(output.revision, input + 40, 32);
    std::memcpy(output.kml_hash, input + 72, 32);
    std::memcpy(output.index_hash, input + 104, 32);
    for (unsigned i = 0; i < 4; ++i)
    {
        output.kml_bytes |= uint32_t(input[136 + i]) << (8 * i);
        output.index_bytes |= uint32_t(input[140 + i]) << (8 * i);
    }
    for (unsigned i = 0; i < 8; ++i) output.availability |= uint64_t(input[144 + i]) << (8 * i);
    return installedPackageBindingValid(output);
}
enum class PackageRouteFile : uint8_t
{
    Kml,
    Temporary,
    Binding,
    BindingTemporary
};
inline bool routePackageTarget(geocaching::ByteView revision, PackageRouteFile file, char* output, size_t capacity)
{
    if (!revision.data || revision.size != 32 || !output || capacity < 80 || uint8_t(file) > 3 || signingSpansOverlap(revision.data, 32, reinterpret_cast<const uint8_t*>(output), 80)) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < 32; ++i) bits |= revision.data[i];
    if (!bits) return false;
    constexpr char prefix[] = "/routes/p-", hex[] = "0123456789abcdef";
    size_t position = sizeof(prefix) - 1;
    std::memcpy(output, prefix, position);
    for (size_t i = 0; i < 32; ++i)
    {
        output[position++] = hex[revision.data[i] >> 4];
        output[position++] = hex[revision.data[i] & 15];
    }
    const char* suffix = file == PackageRouteFile::Kml ? ".kml" : file == PackageRouteFile::Temporary ? ".ktm"
                                                              : file == PackageRouteFile::Binding     ? ".rpk"
                                                                                                      : ".rpt";
    std::memcpy(output + position, suffix, 5);
    return true;
}
} // namespace track_sharing
