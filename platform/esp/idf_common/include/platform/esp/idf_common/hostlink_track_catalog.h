#pragma once

#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/management_envelope.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/idf_common/tracker_snapshot_runtime.h"

namespace hostlink::management
{
inline bool trackCatalogNameValid(std::string_view name)
{
    if (name.empty() || name.size() > 127) return false;
    for (size_t i = 0; i < name.size();)
    {
        const uint8_t first = static_cast<uint8_t>(name[i++]);
        if (first < 0x80)
        {
            if (first < 32 || first == '/' || first == '\\' || first == ':') return false;
            continue;
        }
        unsigned continuation = 0;
        uint32_t value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            continuation = 1;
            value = first & 31;
            minimum = 0x80;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            continuation = 2;
            value = first & 15;
            minimum = 0x800;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            continuation = 3;
            value = first & 7;
            minimum = 0x10000;
        }
        else return false;
        if (continuation > name.size() - i) return false;
        while (continuation--)
        {
            const uint8_t next = static_cast<uint8_t>(name[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = value << 6 | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
inline uint8_t trackCatalogFormat(std::string_view name)
{
    if (name.size() < 4 || name[name.size() - 4] != '.') return 255;
    const auto lower = [](char value)
    { return value >= 'A' && value <= 'Z' ? char(value + 32) : value; };
    const auto a = lower(name[name.size() - 3]), b = lower(name[name.size() - 2]), c = lower(name.back());
    if (a == 'k' && b == 'm' && c == 'l') return 0;
    if (a == 'g' && b == 'p' && c == 'x') return 1;
    if (a == 'c' && b == 's' && c == 'v') return 2;
    if (a == 'b' && b == 'i' && c == 'n') return 3;
    return 255;
}

// Op 80 request [null | [media_session32, directory_position]];
// response [media_session32, next_position|null, [name,format,bytes,active]|null].
// Live directory, not a frozen catalogue: refresh after recording mutations.
// Four entry scans per request, one returned row; scratch borrows the tail of
// PendingCommand. No vector, pathname copy or second file handle is needed.
inline Status readTrackCatalog(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || capacity < 384) return Status::Internal;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t wanted_session = 0, position = 0;
    if (!reader.array(fields, 1) || fields != 1) return Status::InvalidField;
    const bool first = reader.position() < request.body_size && request.body[reader.position()] == 0xc0;
    if (first)
    {
        if (!reader.nil()) return Status::InvalidField;
    }
    else if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(wanted_session) || wanted_session > UINT32_MAX ||
             !reader.unsignedInteger(position) || position > 128U * 1024U * 1024U || position % 32) return Status::InvalidField;
    if (!reader.finished()) return Status::InvalidField;
    if (!platform::ui::tracker::try_acquire_catalog_read()) return Status::Busy;
    struct Release
    {
        ~Release() { platform::ui::tracker::release_snapshot_read(); }
    } release;
    namespace sd = platform::esp::arduino_common::storage;
    const uint32_t session = sd::sd_media_session();
    if (!first && wanted_session != session) return Status::Conflict;
    sd::SdRuntimeDir directory;
    const auto opened = directory.open_read_status("/trackers", session);
    if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != sd::SdFileReadStatus::Ready && opened != sd::SdFileReadStatus::Missing) return Status::StorageUnavailable;
    bool ended = opened == sd::SdFileReadStatus::Missing, found = false, active = false;
    uint8_t format = 255;
    uint64_t bytes = 0;
    char* name = reinterpret_cast<char*>(output + capacity - 128);
    if (!ended && position)
    {
        const auto seek = directory.seek_read_status(position);
        if (seek == sd::SdFileReadStatus::Busy) return Status::Busy;
        if (seek == sd::SdFileReadStatus::Invalid) return Status::Conflict;
        if (seek != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    }
    for (unsigned scan = 0; !ended && !found && scan < 4; ++scan)
    {
        bool is_dir = false;
        uint64_t next = 0;
        const auto result = directory.read_next_info(name, 128, &is_dir, &bytes, &next);
        if (result == sd::SdDirReadStatus::Busy) return Status::Busy;
        if (result == sd::SdDirReadStatus::End)
        {
            ended = true;
            break;
        }
        if (result != sd::SdDirReadStatus::Entry || next <= position || next > 128U * 1024U * 1024U || next % 32) return Status::StorageUnavailable;
        position = next;
        if (is_dir) continue;
        bool control = std::strlen(name) == 10;
        constexpr char reserved[] = "active.bin";
        for (unsigned i = 0; control && i < 10; ++i)
        {
            const char character = name[i] >= 'A' && name[i] <= 'Z' ? char(name[i] + 32) : name[i];
            control = character == reserved[i];
        }
        if (control) continue;
        format = trackCatalogFormat(name);
        if (format == 255) continue;
        if (!trackCatalogNameValid(name)) return Status::StorageUnavailable;
        active = platform::ui::tracker::is_recording_filename(name);
        found = true;
    }
    if (session != sd::sd_media_session() || sd::sd_external_block_owner_active()) return Status::StorageUnavailable;
    geocaching::protocol::CmpWriter writer(output, capacity - 128);
    if (!writer.array(3) || !writer.unsignedInteger(session) || !(ended ? writer.nil() : writer.unsignedInteger(position))) return Status::Internal;
    if (found)
    {
        if (!writer.array(4) || !writer.text(name) || !writer.unsignedInteger(format) || !writer.unsignedInteger(bytes) || !writer.unsignedInteger(active ? 1 : 0)) return Status::Internal;
    }
    else if (!writer.nil()) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace hostlink::management
