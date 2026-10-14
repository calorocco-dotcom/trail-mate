#pragma once
#include "geocaching/protocol/record_decoder.h"
#include "track_sharing/manifest_extensions.h"

namespace track_sharing
{
struct ManifestView
{
    geocaching::ByteView raw, author, nonce, previous, file_hash;
    std::string_view title, description;
    uint64_t created = 0, updated = 0;
    uint32_t revision = 0, distance = 0, duration = 0, ascent = 0, points = 0, file_bytes = 0;
    int32_t west = 0, south = 0, east = 0, north = 0;
    uint8_t kind = 0, state = 0;
    bool has_duration = false, has_ascent = false;
    uint8_t version = 1;
    uint16_t extensions_offset = 0;
};
static_assert(sizeof(ManifestView) <= 208, "Manifest view must not copy text/file storage");
inline bool decodeManifest(geocaching::ByteView raw, ManifestView& output)
{
    output = {};
    if (!raw.data || !raw.size || raw.size > 4096) return false;
    geocaching::protocol::CmpReader reader(raw);
    size_t fields = 0;
    uint64_t value = 0;
    const auto bounded = [&](uint64_t maximum, uint64_t& number)
    { return reader.unsignedInteger(number) && number <= maximum; };
    const auto nonzero = [](geocaching::ByteView bytes)
    {uint8_t bits=0;for(size_t i=0;i<bytes.size;++i)bits|=bytes.data[i];return bits!=0; };
    if (!reader.array(fields, 16) || (fields != 14 && fields != 16) || !bounded(2, value) || !value || fields != (value == 1 ? 14U : 16U)) return false;
    output.version = static_cast<uint8_t>(value);
    if (
        !reader.binary(output.author, 64) || output.author.size != 64 || !nonzero(output.author) ||
        !reader.binary(output.nonce, 16) || output.nonce.size != 16 || !nonzero(output.nonce) || !bounded(1, value)) return false;
    output.kind = static_cast<uint8_t>(value);
    if (!bounded(UINT32_MAX, value) || !value) return false;
    output.revision = static_cast<uint32_t>(value);
    if (output.revision == 1)
    {
        if (!reader.nil()) return false;
    }
    else if (!reader.binary(output.previous, 32) || output.previous.size != 32) return false;
    if (!bounded(1, value) || (output.revision == 1 && value)) return false;
    output.state = static_cast<uint8_t>(value);
    if (!bounded(253402300799ULL, output.created) || !bounded(253402300799ULL, output.updated) || output.updated < output.created ||
        !reader.text(output.title, 128) || !geocaching::protocol::validRecordText(output.title, false, true) ||
        !reader.text(output.description, 2048) || !geocaching::protocol::validRecordText(output.description, true, false) ||
        !reader.array(fields, 4) || fields != 4) return false;
    int64_t coordinate = 0;
    const auto angle = [&](int64_t low, int64_t high, int32_t& result)
    {if(!reader.signedInteger(coordinate)||coordinate<low||coordinate>high)return false;result=static_cast<int32_t>(coordinate);return true; };
    if (!angle(-1800000000, 1799999999, output.west) || !angle(-900000000, 900000000, output.south) ||
        !angle(-1800000000, 1799999999, output.east) || !angle(output.south, 900000000, output.north) || !reader.array(fields, 4) || fields != 4 || !bounded(UINT32_MAX, value)) return false;
    output.distance = static_cast<uint32_t>(value);
    const auto optional = [&](uint32_t& result, bool& present)
    {if(reader.position()<raw.size&&raw.data[reader.position()]==0xc0)return reader.nil();if(!bounded(UINT32_MAX,value))return false;result=static_cast<uint32_t>(value);present=true;return true; };
    if (!optional(output.duration, output.has_duration) || !optional(output.ascent, output.has_ascent) || !bounded(100000, value) || !value) return false;
    output.points = static_cast<uint32_t>(value);
    if (!reader.array(fields, 4) || fields != 4 || !reader.binary(output.file_hash, 32) || output.file_hash.size != 32 || !bounded(8388608, value) || !value) return false;
    output.file_bytes = static_cast<uint32_t>(value);
    std::string_view mime;
    if (!reader.text(mime, 36) || mime != "application/vnd.google-earth.kml+xml" || !bounded(1, value) || value != 1) return false;
    if (output.version == 2)
    {
        output.extensions_offset = static_cast<uint16_t>(reader.position());
        if (!readManifestMetadata(reader, raw, output.points) || !readAttachmentIndexBinding(reader, raw)) return false;
    }
    if (!reader.finished()) return false;
    output.raw = raw;
    return true;
}
// Requires a decoded immutable view; this does not establish author integrity.
inline bool getManifestExtensions(const ManifestView& view, ManifestExtensionsView& output)
{
    output = {};
    if (view.version != 2 || !view.raw.data || view.raw.size > 4096 || !view.extensions_offset || view.extensions_offset >= view.raw.size) return false;
    const geocaching::ByteView tail{view.raw.data + view.extensions_offset, view.raw.size - view.extensions_offset};
    geocaching::protocol::CmpReader reader(tail);
    return readManifestMetadata(reader, tail, view.points, &output.metadata) && readAttachmentIndexBinding(reader, tail, &output.assets) && reader.finished();
}
static_assert(sizeof(ManifestView) == ((offsetof(ManifestView, has_ascent) + sizeof(bool) + alignof(ManifestView) - 1) / alignof(ManifestView)) * alignof(ManifestView),
              "Version/offset must fit the original borrowed view padding on every ABI");
} // namespace track_sharing
