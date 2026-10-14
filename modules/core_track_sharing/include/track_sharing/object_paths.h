#pragma once
#include "geocaching/domain/record.h"
#include <cstring>
namespace track_sharing
{
enum class ObjectFile : uint8_t
{
    HeadA,
    HeadB,
    ReservationA,
    ReservationB,
    SignedTemporary,
    SignedManifest,
    Directory,
    KmlTemporary,
    KmlFile,
    PublicationA,
    PublicationB,
    PublicationDirectory,
    ReceiptTemporary,
    Receipt,
    AttachmentIndex,
    AttachmentIndexTemporary,
    AttachmentResource,
    AttachmentResourceTemporary
};
constexpr const char* kObjectDirectory = "/trailmate/tracks";
constexpr size_t kObjectPathCapacity = 160;
// Device-derived lowercase hex only. key is empty for heads, operation16 for
// reservation slots, revision hash32 for immutable signed manifest files.
inline bool resolveObjectPath(geocaching::ByteView object, ObjectFile file, geocaching::ByteView key, char* output, size_t capacity)
{
    if (!object.data || object.size != 32 || !output || capacity < kObjectPathCapacity || static_cast<uint8_t>(file) > 17) return false;
    const size_t key_size = file == ObjectFile::Directory || file == ObjectFile::HeadA || file == ObjectFile::HeadB ? 0 : file == ObjectFile::ReservationA || file == ObjectFile::ReservationB || file == ObjectFile::PublicationA || file == ObjectFile::PublicationB || file == ObjectFile::PublicationDirectory || file == ObjectFile::ReceiptTemporary || file == ObjectFile::Receipt ? 16
                                                                                                                                                                                                                                                                                                                                                                                          : 32;
    if (key.size != key_size || (key_size && !key.data)) return false;
    const auto overlaps = [&](geocaching::ByteView input)
    { const auto a = reinterpret_cast<uintptr_t>(input.data), b = reinterpret_cast<uintptr_t>(output); return input.size && (a <= b ? b - a < input.size : a - b < kObjectPathCapacity); };
    if (overlaps(object) || overlaps(key)) return false;
    uint8_t object_bits = 0, key_bits = 0;
    for (size_t i = 0; i < 32; ++i) object_bits |= object.data[i];
    for (size_t i = 0; i < key_size; ++i) key_bits |= key.data[i];
    if (!object_bits || (key_size && !key_bits)) return false;
    constexpr char hex[] = "0123456789abcdef", prefix[] = "/trailmate/tracks/";
    size_t position = sizeof(prefix) - 1;
    std::memcpy(output, prefix, position);
    for (size_t i = 0; i < 32; ++i)
    {
        output[position++] = hex[object.data[i] >> 4];
        output[position++] = hex[object.data[i] & 15];
    }
    if (file == ObjectFile::Directory)
    {
        output[position] = 0;
        return true;
    }
    output[position++] = '/';
    if (!key_size)
    {
        std::memcpy(output + position, file == ObjectFile::HeadA ? "head.a" : "head.b", 7);
        return true;
    }
    for (size_t i = 0; i < key_size; ++i)
    {
        output[position++] = hex[key.data[i] >> 4];
        output[position++] = hex[key.data[i] & 15];
    }
    const char* suffix = file == ObjectFile::ReservationA ? ".rva" : file == ObjectFile::ReservationB       ? ".rvb"
                                                                 : file == ObjectFile::PublicationA         ? ".pua"
                                                                 : file == ObjectFile::PublicationB         ? ".pub"
                                                                 : file == ObjectFile::PublicationDirectory ? ".dir"
                                                                 : file == ObjectFile::ReceiptTemporary     ? ".rct"
                                                                 : file == ObjectFile::Receipt              ? ".rcp"
                                                                 : file == ObjectFile::SignedTemporary      ? ".tmp"
                                                                 : file == ObjectFile::KmlTemporary         ? ".ktm"
                                                                 : file == ObjectFile::KmlFile              ? ".kml"
                                                                                                            : ".cmp";
    if (file == ObjectFile::AttachmentIndex) suffix = ".idx";
    else if (file == ObjectFile::AttachmentIndexTemporary) suffix = ".itm";
    else if (file == ObjectFile::AttachmentResource) suffix = ".ast";
    else if (file == ObjectFile::AttachmentResourceTemporary) suffix = ".atm";
    std::memcpy(output + position, suffix, 5);
    static_assert(sizeof(prefix) - 1 + 64 + 1 + 64 + 5 <= kObjectPathCapacity, "Object path exceeds admitted storage");
    return true;
}
} // namespace track_sharing
