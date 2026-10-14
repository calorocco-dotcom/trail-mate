#pragma once

#include "hostlink/track_snapshot_session.h"

namespace hostlink::management
{
struct TrackSnapshotSelection
{
    uint8_t id[16]{};
    uint32_t media_session = 0, bytes = 0;
    std::string_view filename;
    uint8_t format = 0;
};
// Name borrows the command input. The platform must copy it to admitted owner
// storage before overwriting the command payload or scheduling another slice.
inline bool decodeTrackSnapshotSelection(geocaching::ByteView body, TrackSnapshotSelection& output)
{
    output = {};
    geocaching::protocol::CmpReader reader(body);
    size_t fields = 0;
    geocaching::ByteView id;
    uint64_t media = 0, bytes = 0, format = 0;
    if (!reader.array(fields, 5) || fields != 5 || !reader.binary(id, 16) || id.size != 16 ||
        !reader.unsignedInteger(media) || media > UINT32_MAX || !reader.text(output.filename, 117) || output.filename.empty() ||
        !reader.unsignedInteger(bytes) || !bytes || bytes > kTrackSnapshotMaxBytes ||
        !reader.unsignedInteger(format) || format > 3 || !reader.finished()) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < id.size; ++i) bits |= id.data[i];
    if (!bits) return false;
    for (const auto character : output.filename)
        if (static_cast<uint8_t>(character) < 32 || character == '/' || character == '\\' || character == ':') return false;
    const auto lower = [](char value)
    { return value >= 'A' && value <= 'Z' ? char(value + 32) : value; };
    const auto name = output.filename;
    if (name.size() < 5 || name[name.size() - 4] != '.') return false;
    constexpr const char* suffix[] = {"kml", "gpx", "csv", "bin"};
    for (unsigned i = 0; i < 3; ++i)
        if (lower(name[name.size() - 3 + i]) != suffix[format][i]) return false;
    if (name.size() == 10)
    {
        bool control = true;
        constexpr char reserved[] = "active.bin";
        for (unsigned i = 0; i < 10; ++i) control = control && lower(name[i]) == reserved[i];
        if (control) return false;
    }
    std::memcpy(output.id, id.data, 16);
    output.media_session = static_cast<uint32_t>(media);
    output.bytes = static_cast<uint32_t>(bytes);
    output.format = static_cast<uint8_t>(format);
    return true;
}
inline bool encodeTrackSnapshotBuildState(const SnapshotBuildSession& task, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || task.state() == SnapshotSessionState::Empty) return false;
    const auto& metadata = task.metadata();
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(5) || !writer.binary({metadata.id, 16}) || !writer.unsignedInteger(static_cast<uint8_t>(task.state())) ||
        !writer.unsignedInteger(task.copiedBytes()) || !writer.unsignedInteger(metadata.bytes) || !writer.unsignedInteger(metadata.format)) return false;
    written = writer.size();
    return true;
}
} // namespace hostlink::management
