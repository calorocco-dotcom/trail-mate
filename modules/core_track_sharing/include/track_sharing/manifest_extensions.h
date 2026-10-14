#pragma once
#include "geocaching/protocol/record_decoder.h"

namespace track_sharing
{
constexpr size_t kMaxAttachmentIndexBytes = 294912;
constexpr uint32_t kMaxTrackPackageBytes = 64U * 1024U * 1024U;
constexpr size_t kMaxTrackAttachments = 64;
struct ManifestMetadataView
{
    geocaching::ByteView raw, tags, derived_object, derived_revision;
    uint32_t geometry_capabilities = 0, segments = 0, waypoints = 0;
    uint8_t activity = 0, time_coverage = 0, altitude_semantics = 0, tag_count = 0;
};
struct AttachmentIndexBinding
{
    geocaching::ByteView hash;
    uint32_t bytes = 0;
    uint8_t count = 0;
};
struct ManifestExtensionsView
{
    ManifestMetadataView metadata;
    AttachmentIndexBinding assets;
};
static_assert(sizeof(ManifestExtensionsView) <= 112, "Extensions borrow all variable data");

// Optional output lets the main decoder validate without an automatic
// extension object or retained tag/attachment arrays.
inline bool readManifestMetadata(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw,
                                 uint32_t points, ManifestMetadataView* output = nullptr)
{
    if (output) *output = {};
    const size_t start = reader.position();
    if (!raw.data || start >= raw.size) return false;
    size_t fields = 0, count = 0;
    uint64_t value = 0;
    if (!reader.array(fields, 8) || fields != 8 || !reader.unsignedInteger(value) || value > 255) return false;
    if (output) output->activity = static_cast<uint8_t>(value);
    const size_t tag_start = reader.position();
    if (!reader.array(count, 8)) return false;
    const size_t tag_body = reader.position();
    if (tag_body > raw.size) return false;
    for (size_t i = 0; i < count; ++i)
    {
        std::string_view tag, previous;
        if (!reader.text(tag, 48) || !geocaching::protocol::validRecordText(tag, false, true)) return false;
        geocaching::protocol::CmpReader earlier({raw.data + tag_body, raw.size - tag_body});
        for (size_t j = 0; j < i; ++j)
            if (!earlier.text(previous, 48) || previous == tag) return false;
    }
    if (output) output->tags = {raw.data + tag_start, reader.position() - tag_start}, output->tag_count = static_cast<uint8_t>(count);
    if (!reader.unsignedInteger(value) || value > UINT32_MAX) return false;
    if (output) output->geometry_capabilities = static_cast<uint32_t>(value);
    if (!reader.unsignedInteger(value) || value > points) return false;
    if (output) output->segments = static_cast<uint32_t>(value);
    if (!reader.unsignedInteger(value) || value > points) return false;
    if (output) output->waypoints = static_cast<uint32_t>(value);
    if (!reader.unsignedInteger(value) || value > 2) return false;
    if (output) output->time_coverage = static_cast<uint8_t>(value);
    if (!reader.unsignedInteger(value) || value > 4) return false;
    if (output) output->altitude_semantics = static_cast<uint8_t>(value);
    geocaching::ByteView object, revision;
    if (reader.position() < raw.size && raw.data[reader.position()] == 0xc0)
    {
        if (!reader.nil()) return false;
    }
    else if (!reader.array(fields, 2) || fields != 2 || !reader.binary(object, 32) || object.size != 32 || !reader.binary(revision, 32) || revision.size != 32) return false;
    if (output)
    {
        output->derived_object = object;
        output->derived_revision = revision;
        output->raw = {raw.data + start, reader.position() - start};
    }
    return true;
}
inline bool readAttachmentIndexBinding(geocaching::protocol::CmpReader& reader, geocaching::ByteView raw, AttachmentIndexBinding* output = nullptr)
{
    if (output) *output = {};
    if (reader.position() < raw.size && raw.data[reader.position()] == 0xc0) return reader.nil();
    size_t fields = 0;
    uint64_t bytes = 0, count = 0, format = 0;
    geocaching::ByteView hash;
    if (!reader.array(fields, 4) || fields != 4 || !reader.binary(hash, 32) || hash.size != 32 ||
        !reader.unsignedInteger(bytes) || !bytes || bytes > kMaxAttachmentIndexBytes ||
        !reader.unsignedInteger(count) || !count || count > kMaxTrackAttachments || !reader.unsignedInteger(format) || format != 1) return false;
    if (output) *output = {hash, static_cast<uint32_t>(bytes), static_cast<uint8_t>(count)};
    return true;
}
} // namespace track_sharing
