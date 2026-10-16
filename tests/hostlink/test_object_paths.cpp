#include "track_sharing/object_paths.h"
#include "track_sharing/publication_slot_path.h"
#include <cassert>
#include <string>
int main()
{
    using namespace track_sharing;
    uint8_t object[32], key[32];
    for (unsigned i = 0; i < 32; ++i)
    {
        object[i] = uint8_t(i * 8);
        key[i] = 0xab;
    }
    char output[kObjectPathCapacity];
    const std::string root = "/trailmate/tracks/0008101820283038404850586068707880889098a0a8b0b8c0c8d0d8e0e8f0f8/";
    assert(resolveObjectPath({object, 32}, ObjectFile::Directory, {}, output, sizeof(output)) && output == root.substr(0, root.size() - 1));
    assert(resolveObjectPath({object, 32}, ObjectFile::HeadA, {}, output, sizeof(output)) && output == root + "head.a");
    assert(resolveObjectPath({object, 32}, ObjectFile::HeadB, {}, output, sizeof(output)) && output == root + "head.b");
    std::string operation, revision;
    for (unsigned i = 0; i < 32; ++i)
    {
        revision += "ab";
        if (i < 16) operation += "ab";
    }
    assert(resolveObjectPath({object, 32}, ObjectFile::ReservationA, {key, 16}, output, sizeof(output)) && output == root + operation + ".rva");
    assert(resolveObjectPath({object, 32}, ObjectFile::ReservationB, {key, 16}, output, sizeof(output)) && output == root + operation + ".rvb");
    assert(resolveObjectPath({object, 32}, ObjectFile::SignedTemporary, {key, 32}, output, sizeof(output)) && output == root + revision + ".tmp");
    assert(resolveObjectPath({object, 32}, ObjectFile::SignedManifest, {key, 32}, output, sizeof(output)) && output == root + revision + ".cmp");
    assert(resolveObjectPath({object, 32}, ObjectFile::KmlTemporary, {key, 32}, output, sizeof(output)) && output == root + revision + ".ktm");
    assert(resolveObjectPath({object, 32}, ObjectFile::KmlFile, {key, 32}, output, sizeof(output)) && output == root + revision + ".kml");
    assert(resolveObjectPath({object, 32}, ObjectFile::PublicationA, {key, 16}, output, sizeof(output)) && output == root + operation + ".pua");
    assert(resolveObjectPath({object, 32}, ObjectFile::PublicationB, {key, 16}, output, sizeof(output)) && output == root + operation + ".pub");
    assert(resolveObjectPath({object, 32}, ObjectFile::PublicationDirectory, {key, 16}, output, sizeof(output)) && output == root + operation + ".dir");
    assert(resolveObjectPath({object, 32}, ObjectFile::ReceiptTemporary, {key, 16}, output, sizeof(output)) && output == root + operation + ".rct");
    assert(resolveObjectPath({object, 32}, ObjectFile::Receipt, {key, 16}, output, sizeof(output)) && output == root + operation + ".rcp");
    const auto preserved = std::string(output);
    PublicationSlotKey parsed;
    for (const auto slot : {ObjectFile::PublicationA, ObjectFile::PublicationB})
    {
        assert(resolveObjectPath({object, 32}, slot, {key, 16}, output, sizeof(output)));
        const std::string canonical(output);
        assert(parsePublicationSlotPath(output, canonical.size(), parsed));
        assert(!std::memcmp(parsed.object, object, 32) && !std::memcmp(parsed.operation, key, 16) && parsed.slot == slot);
        for (auto invalid : {canonical + "x", canonical.substr(1), root + operation + ".rct", root + operation + ".rcp"})
            assert(!parsePublicationSlotPath(invalid.data(), invalid.size(), parsed));
        auto invalid = canonical;
        invalid[17] = 'A';
        assert(!parsePublicationSlotPath(invalid.data(), invalid.size(), parsed));
        invalid = canonical;
        invalid.replace(82, 32, 32, '0');
        assert(!parsePublicationSlotPath(invalid.data(), invalid.size(), parsed));
    }
    std::memcpy(output, preserved.c_str(), preserved.size() + 1);
    assert(!resolveObjectPath({object, 32}, ObjectFile::SignedManifest, {key, 16}, output, sizeof(output)) && output == preserved);
    assert(!resolveObjectPath({object, 32}, ObjectFile::HeadA, {key, 16}, output, sizeof(output)) && output == preserved);
    assert(!resolveObjectPath({object, 32}, static_cast<ObjectFile>(7), {}, output, sizeof(output)) && output == preserved);
    assert(!resolveObjectPath({object, 32}, static_cast<ObjectFile>(9), {key, 32}, output, sizeof(output)) && output == preserved);
    assert(!resolveObjectPath({object, 32}, static_cast<ObjectFile>(14), {key, 16}, output, sizeof(output)) && output == preserved);
    assert(!resolveObjectPath({reinterpret_cast<uint8_t*>(output + 10), 32}, ObjectFile::HeadA, {}, output, sizeof(output)) && output == preserved);
    std::memset(object, 0, sizeof(object));
    assert(!resolveObjectPath({object, 32}, ObjectFile::HeadA, {}, output, sizeof(output)) && output == preserved);
}
