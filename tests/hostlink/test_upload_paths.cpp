#include "track_sharing/upload_paths.h"
#include <cassert>
#include <string>
int main()
{
    using namespace track_sharing;
    uint8_t id[16];
    for (unsigned i = 0; i < 16; ++i) id[i] = uint8_t(i * 17);
    char path[64];
    const std::string base = "/trailmate/web-upload/00112233445566778899aabbccddeeff";
    assert(resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path)) && path == base + ".kml");
    assert(resolveUploadPath(id, 16, UploadFile::JournalA, path, sizeof(path)) && path == base + ".uja");
    assert(resolveUploadPath(id, 16, UploadFile::JournalB, path, sizeof(path)) && path == base + ".ujb");
    const auto saved = std::string(path);
    assert(uploadByteLimit(UploadKind::Kml) == 8388608 && uploadByteLimit(UploadKind::Manifest) == 4096);
    assert(uploadByteLimit(static_cast<UploadKind>(5)) == 0);
    assert(uploadByteLimit(UploadKind::IpReceipt) == 1100);
    assert(uploadDirectory(static_cast<UploadKind>(5)) == nullptr);
    const std::string receipt = "/trailmate/web-receipt/00112233445566778899aabbccddeeff";
    assert(resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path), UploadKind::IpReceipt) && path == receipt + ".cmp");
    assert(resolveUploadPath(id, 16, UploadFile::JournalA, path, sizeof(path), UploadKind::IpReceipt) && path == receipt + ".uja");
    assert(resolveUploadPath(id, 16, UploadFile::JournalB, path, sizeof(path), UploadKind::IpReceipt) && path == receipt + ".ujb");
    assert(resolveUploadPath(id, 16, UploadFile::JournalB, path, sizeof(path)) && path == saved);
    assert(!resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path), static_cast<UploadKind>(5)) && path == saved);
    const std::string manifest = "/trailmate/web-manifest/00112233445566778899aabbccddeeff";
    assert(resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path), UploadKind::Manifest) && path == manifest + ".cmp");
    assert(resolveUploadPath(id, 16, UploadFile::JournalA, path, sizeof(path), UploadKind::Manifest) && path == manifest + ".uja");
    assert(resolveUploadPath(id, 16, UploadFile::JournalB, path, sizeof(path), UploadKind::Manifest) && path == manifest + ".ujb");
    assert(resolveUploadPath(id, 16, UploadFile::JournalB, path, sizeof(path)) && path == saved);
    assert(!resolveUploadPath(id, 15, UploadFile::Candidate, path, sizeof(path)) && path == saved);
    assert(!resolveUploadPath(id, 16, UploadFile::Candidate, path, 63) && path == saved);
    assert(!resolveUploadPath(id, 16, static_cast<UploadFile>(3), path, sizeof(path)) && path == saved);
    assert(!resolveUploadPath(reinterpret_cast<uint8_t*>(path + 10), 16, UploadFile::Candidate, path, sizeof(path)) && path == saved);
    std::memset(id, 0, sizeof(id));
    assert(!resolveUploadPath(id, 16, UploadFile::Candidate, path, sizeof(path)) && path == saved);
}
