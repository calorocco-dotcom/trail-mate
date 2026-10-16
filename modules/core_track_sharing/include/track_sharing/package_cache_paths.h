#pragma once
#include "track_sharing/object_paths.h"
#include "track_sharing/sign_manifest.h"
namespace track_sharing
{
constexpr const char* kPackageCacheDirectory = "/trailmate/packages";
// Public imported versions are separate from owned object heads/reservations.
// Revision SHA selects the package; resource SHA selects bytes. No user path.
inline bool resolvePackageCachePath(geocaching::ByteView revision, ObjectFile file, geocaching::ByteView key, char* output, size_t capacity)
{
    const bool resource = file == ObjectFile::AttachmentResource || file == ObjectFile::AttachmentResourceTemporary;
    const bool supported = resource || file == ObjectFile::Directory || file == ObjectFile::SignedManifest || file == ObjectFile::SignedTemporary ||
                           file == ObjectFile::KmlFile || file == ObjectFile::KmlTemporary || file == ObjectFile::AttachmentIndex || file == ObjectFile::AttachmentIndexTemporary;
    if (!supported || !revision.data || revision.size != 32 || !output || capacity < 160 ||
        (resource ? (!key.data || key.size != 32) : key.size != 0) || signingSpansOverlap(revision.data, 32, reinterpret_cast<const uint8_t*>(output), 160) ||
        signingSpansOverlap(key.data, key.size, reinterpret_cast<const uint8_t*>(output), 160)) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < 32; ++i) bits |= revision.data[i];
    if (!bits) return false;
    if (resource)
    {
        bits = 0;
        for (size_t i = 0; i < 32; ++i) bits |= key.data[i];
        if (!bits) return false;
    }
    constexpr char prefix[] = "/trailmate/packages/", hex[] = "0123456789abcdef";
    size_t position = sizeof(prefix) - 1;
    std::memcpy(output, prefix, position);
    for (size_t i = 0; i < 32; ++i)
    {
        output[position++] = hex[revision.data[i] >> 4];
        output[position++] = hex[revision.data[i] & 15];
    }
    if (file == ObjectFile::Directory)
    {
        output[position] = 0;
        return true;
    }
    output[position++] = '/';
    if (resource)
    {
        for (size_t i = 0; i < 32; ++i)
        {
            output[position++] = hex[key.data[i] >> 4];
            output[position++] = hex[key.data[i] & 15];
        }
        std::memcpy(output + position, file == ObjectFile::AttachmentResource ? ".ast" : ".atm", 5);
    }
    else
    {
        const char* name = file == ObjectFile::SignedManifest ? "manifest.cmp" : file == ObjectFile::SignedTemporary ? "manifest.tmp"
                                                                             : file == ObjectFile::KmlFile           ? "route.kml"
                                                                             : file == ObjectFile::KmlTemporary      ? "route.ktm"
                                                                             : file == ObjectFile::AttachmentIndex   ? "index.cmp"
                                                                                                                     : "index.itm";
        std::memcpy(output + position, name, std::strlen(name) + 1);
    }
    static_assert(sizeof(prefix) - 1 + 64 + 1 + 64 + 5 <= 160, "Public package path exceeds borrowed storage");
    return true;
}
} // namespace track_sharing
