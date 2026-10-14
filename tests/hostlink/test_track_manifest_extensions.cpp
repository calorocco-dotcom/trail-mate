#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/attachment_index.h"
#include "track_sharing/manifest_chain.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
static bool readBytes(std::vector<uint8_t>& output)
{
    std::string hex;
    if (!std::getline(std::cin, hex) || hex.empty() || hex.size() % 2 || hex.size() > 2 * track_sharing::kMaxAttachmentIndexBytes) return false;
    const auto digit = [](char c)
    { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                   : -1; };
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        if (digit(hex[i]) < 0 || digit(hex[i + 1]) < 0) return false;
        output.push_back(static_cast<uint8_t>(digit(hex[i]) * 16 + digit(hex[i + 1])));
    }
    return true;
}
int main()
{
    std::string mode;
    if (!std::getline(std::cin, mode))
    {
        assert(track_sharing::attachmentReferenceValid("images/山.jpg"));
        assert(!track_sharing::attachmentReferenceValid("images/%2e%2e/a.jpg"));
        assert(!track_sharing::attachmentReferenceValid("javascript:bad"));
        assert(track_sharing::attachmentMimeValid("image/png"));
        assert(!track_sharing::attachmentMimeValid("image/png;bad"));
        assert(sizeof(track_sharing::ManifestView) == ((offsetof(track_sharing::ManifestView, has_ascent) + sizeof(bool) + alignof(track_sharing::ManifestView) - 1) / alignof(track_sharing::ManifestView)) * alignof(track_sharing::ManifestView));
        return 0;
    }
    std::vector<uint8_t> a, b, parent_hash;
    if (!readBytes(a) || !readBytes(b)) return 1;
    track_sharing::ManifestView view;
    if (!track_sharing::decodeManifest({b.data(), b.size()}, view)) return 1;
    if (mode == "CHAIN")
    {
        track_sharing::ManifestView previous;
        if (!readBytes(parent_hash) || parent_hash.size() != 32 || !track_sharing::decodeManifest({a.data(), a.size()}, previous)) return 1;
        return track_sharing::validateSuccessor(previous, view, parent_hash.data()) ? 0 : 1;
    }
    if (mode != "INDEX") return 1;
    track_sharing::ManifestExtensionsView extensions;
    if (!track_sharing::getManifestExtensions(view, extensions)) return 1;
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    track_sharing::AttachmentIndexCursor cursor({});
    track_sharing::AttachmentEntryView entry;
    uint8_t digest[32];
    if (!track_sharing::validateAttachmentIndexBinding({a.data(), a.size()}, extensions.assets, view.file_hash, view.file_bytes, hash, digest, cursor, entry)) return 1;
    assert(cursor.finish());
    std::cout << cursor.count() << ',' << cursor.requiredCount() << ',' << cursor.packageBytes() << '\n';
}
