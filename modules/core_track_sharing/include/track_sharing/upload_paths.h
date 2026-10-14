#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace track_sharing
{
enum class UploadKind : uint8_t
{
    Kml = 3,
    Manifest = 4,
    IpReceipt = 7,
    AttachmentIndex = 8,
    AttachmentResource = 9,
    AttachmentBindings = 10,
    SignedManifest = 11
};
constexpr uint32_t uploadByteLimit(UploadKind kind)
{
    return kind == UploadKind::Kml ? 8388608 : kind == UploadKind::Manifest         ? 4096
                                           : kind == UploadKind::IpReceipt          ? 1100
                                           : kind == UploadKind::AttachmentIndex    ? 294912
                                           : kind == UploadKind::AttachmentResource ? 8388608
                                           : kind == UploadKind::AttachmentBindings ? 1100
                                           : kind == UploadKind::SignedManifest     ? 4166
                                                                                    : 0;
}
enum class UploadFile : uint8_t
{
    Candidate,
    JournalA,
    JournalB
};
constexpr const char* kUploadDirectory = "/trailmate/web-upload";
constexpr const char* kManifestUploadDirectory = "/trailmate/web-manifest";
constexpr const char* kReceiptUploadDirectory = "/trailmate/web-receipt";
constexpr const char* kAttachmentIndexUploadDirectory = "/trailmate/web-index";
constexpr const char* kAttachmentResourceUploadDirectory = "/trailmate/web-asset";
constexpr const char* kAttachmentBindingsUploadDirectory = "/trailmate/web-package";
constexpr const char* kSignedManifestUploadDirectory = "/trailmate/web-signed";
constexpr const char* uploadDirectory(UploadKind kind)
{
    return kind == UploadKind::Kml ? kUploadDirectory : kind == UploadKind::Manifest         ? kManifestUploadDirectory
                                                    : kind == UploadKind::IpReceipt          ? kReceiptUploadDirectory
                                                    : kind == UploadKind::AttachmentIndex    ? kAttachmentIndexUploadDirectory
                                                    : kind == UploadKind::AttachmentResource ? kAttachmentResourceUploadDirectory
                                                    : kind == UploadKind::AttachmentBindings ? kAttachmentBindingsUploadDirectory
                                                    : kind == UploadKind::SignedManifest     ? kSignedManifestUploadDirectory
                                                                                             : nullptr;
}
constexpr size_t kUploadPathCapacity = 64;
// No user path/name enters this resolver. Caller borrows a command tail or
// admitted task storage; exclusive lease and media authorization are separate.
inline bool resolveUploadPath(const uint8_t* id, size_t size, UploadFile file, char* output, size_t capacity, UploadKind kind = UploadKind::Kml)
{
    if (!id || size != 16 || !output || capacity < kUploadPathCapacity || static_cast<uint8_t>(file) > 2 || !uploadByteLimit(kind)) return false;
    const auto source = reinterpret_cast<uintptr_t>(id), destination = reinterpret_cast<uintptr_t>(output);
    if (source <= destination ? destination - source < 16 : source - destination < kUploadPathCapacity) return false;
    uint8_t nonzero = 0;
    for (size_t i = 0; i < 16; ++i) nonzero |= id[i];
    if (!nonzero) return false;
    constexpr char hex[] = "0123456789abcdef";
    const char* directory = uploadDirectory(kind);
    size_t position = std::strlen(directory);
    std::memcpy(output, directory, position);
    output[position++] = '/';
    for (size_t i = 0; i < 16; ++i)
    {
        output[position++] = hex[id[i] >> 4];
        output[position++] = hex[id[i] & 15];
    }
    const char* suffix = file == UploadFile::JournalA ? ".uja" : file == UploadFile::JournalB ? ".ujb"
                                                             : kind == UploadKind::Kml        ? ".kml"
                                                                                              : ".cmp";
    std::memcpy(output + position, suffix, 5);
    static_assert(sizeof("/trailmate/web-manifest/") - 1 + 32 + 5 <= kUploadPathCapacity, "Upload path exceeds borrowed tail");
    return true;
}
} // namespace track_sharing
