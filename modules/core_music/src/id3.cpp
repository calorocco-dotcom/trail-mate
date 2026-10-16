#include "music/id3.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace music
{
namespace
{
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
bool syncsafe(const uint8_t* p, uint32_t& value)
{
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return false;
    value = uint32_t(p[0]) << 21 | uint32_t(p[1]) << 14 | uint32_t(p[2]) << 7 | p[3];
    return true;
}
void copy(char* out, std::size_t capacity, const char* text)
{
    if (!capacity) return;
    std::size_t n = std::min(capacity - 1, std::strlen(text));
    while (n && (static_cast<uint8_t>(text[n]) & 0xc0) == 0x80) --n;
    std::memcpy(out, text, n);
    out[n] = 0;
}
const char* frame_name(const char* id)
{
    static constexpr struct
    {
        const char* id;
        const char* name;
    } names[] = {
        {"TIT2", "Title"}, {"TIT1", "Content group"}, {"TIT3", "Subtitle"}, {"TPE1", "Artist"}, {"TPE2", "Album artist"}, {"TPE3", "Conductor"}, {"TPE4", "Remixed by"}, {"TALB", "Album"}, {"TCOM", "Composer"}, {"TEXT", "Lyricist"}, {"TCON", "Genre"}, {"TYER", "Year"}, {"TDRC", "Recording date"}, {"TDRL", "Release date"}, {"TDOR", "Original release"}, {"TRCK", "Track number"}, {"TPOS", "Disc number"}, {"TBPM", "BPM"}, {"TKEY", "Initial key"}, {"TLAN", "Language"}, {"TCOP", "Copyright"}, {"TPUB", "Publisher"}, {"TENC", "Encoded by"}, {"TSSE", "Encoder settings"}, {"TSRC", "ISRC"}, {"TLEN", "Length (ms)"}, {"TOAL", "Original album"}, {"TOPE", "Original artist"}, {"TOWN", "Owner"}, {"TMED", "Media type"}, {"TSOA", "Album sort"}, {"TSOP", "Artist sort"}, {"TSOT", "Title sort"}, {"COMM", "Comment"}, {"USLT", "Embedded lyrics"}, {"WXXX", "Website"}, {"TXXX", "User text"}, {"IPLS", "Involved people"}, {"TIPL", "Involved people"}, {"TMCL", "Musician credits"}, {"POPM", "Rating"}, {"PCNT", "Play count"}};
    for (const auto& name : names)
        if (!std::strcmp(id, name.id)) return name.name;
    return id;
}
const char* canonical(const char* id)
{
    static constexpr struct
    {
        const char* old;
        const char* current;
    } names[] = {
        {"TT2", "TIT2"}, {"TP1", "TPE1"}, {"TP2", "TPE2"}, {"TAL", "TALB"}, {"TCO", "TCON"}, {"TCM", "TCOM"}, {"TYE", "TYER"}, {"TRK", "TRCK"}, {"TPA", "TPOS"}, {"TCR", "TCOP"}, {"TEN", "TENC"}, {"TSS", "TSSE"}, {"COM", "COMM"}, {"ULT", "USLT"}, {"PIC", "APIC"}, {"TXX", "TXXX"}, {"WXX", "WXXX"}, {"TLE", "TLEN"}, {"TXT", "TEXT"}, {"TBP", "TBPM"}};
    for (const auto& name : names)
        if (!std::strcmp(id, name.old)) return name.current;
    return id;
}
} // namespace
bool TagReader::exact(uint32_t offset, void* output, std::size_t size)
{
    return offset <= input_->size() && size <= input_->size() - offset && input_->read_at(offset, output, size) == size;
}
bool TagReader::byte(uint32_t offset, uint8_t& value)
{
    if (offset >= input_->size()) return false;
    if (offset < byte_offset_ || offset - byte_offset_ >= byte_count_)
    {
        byte_offset_ = offset;
        byte_count_ = static_cast<uint16_t>(std::min<uint32_t>(sizeof(byte_cache_), input_->size() - offset));
        if (!exact(offset, byte_cache_, byte_count_))
        {
            byte_count_ = 0;
            return false;
        }
    }
    value = byte_cache_[offset - byte_offset_];
    return true;
}
uint32_t TagReader::terminated(uint32_t start, uint32_t end, uint8_t encoding)
{
    const uint32_t step = encoding == 1 || encoding == 2 ? 2 : 1;
    uint8_t a = 0, b = 0;
    for (uint32_t at = start; at < end && at - start < 4096; at += step)
    {
        if (!byte(at, a) || (step == 2 && (at + 1 >= end || !byte(at + 1, b)))) return end;
        if (a == 0 && (step == 1 || b == 0)) return at + step;
    }
    return end;
}
bool TagReader::text(uint32_t start, uint32_t end, uint8_t encoding, char* out, std::size_t capacity, bool unsync)
{
    if (!capacity) return true;
    out[0] = 0;
    if (start >= end || encoding > 3) return false;
    std::size_t used = 0, available = 0, cursor = 0;
    uint32_t at = start;
    bool previous_ff = false, truncated = false;
    auto next = [&](uint8_t& value)
    {
        for (;;)
        {
            if (cursor == available)
            {
                if (at >= end) return false;
                available = std::min<std::size_t>(sizeof(buffer_), end - at);
                cursor = 0;
                if (!exact(at, buffer_, available)) return false;
                at += available;
            }
            value = buffer_[cursor++];
            if (unsync && previous_ff && value == 0)
            {
                previous_ff = false;
                continue;
            }
            previous_ff = value == 0xff;
            return true;
        }
    };
    auto emit = [&](uint32_t cp)
    {
        const unsigned count = cp < 0x80 ? 1 : cp < 0x800 ? 2
                                           : cp < 0x10000 ? 3
                                                          : 4;
        if (used + count >= capacity)
        {
            truncated = true;
            return false;
        }
        if (count == 1) out[used++] = static_cast<char>(cp);
        else
        {
            out[used++] = static_cast<char>((count == 2 ? 0xc0 : count == 3 ? 0xe0
                                                                            : 0xf0) |
                                            (cp >> (6 * (count - 1))));
            for (unsigned n = count - 1; n; --n) out[used++] = static_cast<char>(0x80 | ((cp >> (6 * (n - 1))) & 0x3f));
        }
        return true;
    };
    uint8_t a = 0, b = 0;
    bool little = encoding == 1, first = true, separator = false;
    while (next(a))
    {
        uint32_t cp = a;
        if (encoding == 1 || encoding == 2)
        {
            if (!next(b)) break;
            if ((first || separator) && ((a == 0xff && b == 0xfe) || (a == 0xfe && b == 0xff)))
            {
                little = a == 0xff;
                first = false;
                continue;
            }
            cp = little ? uint32_t(a) | uint32_t(b) << 8 : uint32_t(a) << 8 | b;
            if (cp >= 0xd800 && cp <= 0xdbff)
            {
                if (!next(a) || !next(b)) break;
                const uint32_t lo = little ? uint32_t(a) | uint32_t(b) << 8 : uint32_t(a) << 8 | b;
                cp = lo >= 0xdc00 && lo <= 0xdfff ? 0x10000 + ((cp - 0xd800) << 10) + lo - 0xdc00 : 0xfffd;
            }
            else if (cp >= 0xdc00 && cp <= 0xdfff) cp = 0xfffd;
        }
        else if (encoding == 3 && a >= 0x80)
        {
            const unsigned count = a >= 0xc2 && a <= 0xdf ? 2 : a >= 0xe0 && a <= 0xef ? 3
                                                            : a >= 0xf0 && a <= 0xf4   ? 4
                                                                                       : 0;
            cp = count ? a & ((1U << (7 - count)) - 1) : 0xfffd;
            for (unsigned n = 1; n < count; ++n)
            {
                if (!next(b) || (b & 0xc0) != 0x80)
                {
                    cp = 0xfffd;
                    break;
                }
                cp = cp << 6 | (b & 0x3f);
            }
            if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
        }
        first = false;
        if (!cp)
        {
            separator = used != 0;
            continue;
        }
        if (separator)
        {
            if (!emit(' ') || !emit('/') || !emit(' ')) break;
            separator = false;
        }
        if (cp < 32 && cp != '\n' && cp != '\t') cp = ' ';
        if (!emit(cp)) break;
    }
    while (used && (out[used - 1] == ' ' || out[used - 1] == '\n' || out[used - 1] == '\r')) --used;
    out[used] = 0;
    return truncated;
}
void TagReader::field(const char* id, const char* name, const char* value, bool truncated)
{
    if (!value[0]) return;
    const auto index = fields_++;
    if (!page_ || index < page_->offset || index >= page_->offset + kMetadataRows) return;
    auto& dest = page_->fields[page_->count++];
    copy(dest.id, sizeof(dest.id), id);
    copy(dest.name, sizeof(dest.name), name);
    copy(dest.value, sizeof(dest.value), value);
    dest.truncated = truncated || std::strlen(value) >= sizeof(dest.value);
    page_->truncated |= dest.truncated;
}
void TagReader::parse_picture(uint32_t start, uint32_t end, bool legacy, Track& track)
{
    uint8_t encoding = 0, type = 0;
    if (!byte(start++, encoding) || encoding > 3) return;
    PictureFormat format = PictureFormat::None;
    if (legacy)
    {
        uint8_t mime[3];
        if (!exact(start, mime, 3)) return;
        start += 3;
        format = !std::memcmp(mime, "JPG", 3) ? PictureFormat::Jpeg : !std::memcmp(mime, "PNG", 3) ? PictureFormat::Png
                                                                                                   : PictureFormat::None;
    }
    else
    {
        const auto next = terminated(start, end, 0);
        if (next <= start || next - start > 64) return;
        text(start, next, 0, value_, sizeof(value_));
        start = next;
        format = !std::strcmp(value_, "image/jpeg") || !std::strcmp(value_, "image/jpg") ? PictureFormat::Jpeg : !std::strcmp(value_, "image/png") ? PictureFormat::Png
                                                                                                                                                   : PictureFormat::None;
    }
    if (start >= end || !byte(start++, type)) return;
    start = terminated(start, end, encoding);
    if (start >= end) return;
    std::snprintf(value_, sizeof(value_), "%s | type %u | %lu bytes", format == PictureFormat::Jpeg ? "JPEG" : format == PictureFormat::Png ? "PNG"
                                                                                                                                            : "Unsupported image",
                  type, static_cast<unsigned long>(end - start));
    field("APIC", type == 3 ? "Front cover" : "Attached picture", value_);
    if (format == PictureFormat::None || (track.picture_size && (track.picture_type == 3 || type != 3))) return;
    track.picture_offset = start;
    track.picture_size = end - start;
    track.picture_format = format;
    track.picture_type = type;
}
void TagReader::parse_frame(const char* id, uint32_t start, uint32_t end, bool unsync, Track& track)
{
    const bool legacy = std::strlen(id) == 3;
    id = canonical(id);
    if (!std::strcmp(id, "APIC"))
    {
        if (!unsync) parse_picture(start, end, legacy, track);
        else
        {
            field("APIC", "Attached picture", "Unsynchronized picture");
            if (page_) page_->unsupported_flags = true;
        }
        return;
    }
    uint8_t encoding = 0;
    if (id[0] == 'T' || !std::strcmp(id, "COMM") || !std::strcmp(id, "USLT") || !std::strcmp(id, "WXXX") || !std::strcmp(id, "IPLS"))
    {
        if (!byte(start++, encoding) || encoding > 3) return;
        if (!std::strcmp(id, "WXXX"))
        {
            // Only the description uses the declared encoding; URL is Latin-1.
            const auto next = terminated(start, end, encoding);
            bool truncated = text(start, next, encoding, value_, sizeof(value_), unsync);
            auto used = std::strlen(value_);
            if (next < end && used + 4 < sizeof(value_))
            {
                if (used)
                {
                    std::memcpy(value_ + used, " / ", 3);
                    used += 3;
                }
                truncated |= text(next, end, 0, value_ + used, sizeof(value_) - used, unsync);
            }
            field(id, frame_name(id), value_, truncated);
            return;
        }
        if (!std::strcmp(id, "COMM") || !std::strcmp(id, "USLT"))
        {
            if (end - start < 3) return;
            uint8_t a = 0, b = 0;
            if (encoding == 1 && byte(start + 3, a) && byte(start + 4, b) && a == 0xfe && b == 0xff) encoding = 2;
            start = terminated(start + 3, end, encoding);
            if (!std::strcmp(id, "USLT"))
            {
                if (!unsync && start < end)
                {
                    track.lyric_offset = start;
                    track.lyric_size = end - start;
                    track.lyric_encoding = encoding;
                }
                field(id, frame_name(id), unsync ? "Unsynchronized text" : "Available in Lyrics");
                return;
            }
        }
        // User-defined description and value are both retained, separated by '/' by text().
        const bool truncated = text(start, end, encoding, value_, sizeof(value_), unsync);
        field(id, frame_name(id), value_, truncated);
        if (!std::strcmp(id, "TIT2")) copy(track.title, sizeof(track.title), value_);
        else if (!std::strcmp(id, "TPE1")) copy(track.artist, sizeof(track.artist), value_);
        else if (!std::strcmp(id, "TALB")) copy(track.album, sizeof(track.album), value_);
        else if (!std::strcmp(id, "TCON")) copy(track.genre, sizeof(track.genre), value_);
        else if (!std::strcmp(id, "TLEN"))
        {
            const auto length = std::strtoull(value_, nullptr, 10);
            if (length <= UINT32_MAX) track.duration_ms = static_cast<uint32_t>(length);
        }
    }
    else if (id[0] == 'W')
    {
        field(id, "Website", (text(start, end, 0, value_, sizeof(value_)), value_));
    }
    else
    {
        // Binary/private frames are represented without interpreting opaque or encrypted payloads.
        std::snprintf(value_, sizeof(value_), "%lu bytes", static_cast<unsigned long>(end - start));
        field(id, frame_name(id), value_);
    }
}
bool TagReader::read(Reader& input, Track& track, MetadataPage* page, uint16_t offset)
{
    input_ = &input;
    byte_count_ = 0;
    page_ = page;
    fields_ = 0;
    if (page)
    {
        page->~MetadataPage();
        new (page) MetadataPage{};
        page->track_id = track.id;
        page->offset = offset;
    }
    track.picture_offset = track.picture_size = track.lyric_offset = track.lyric_size = track.audio_offset = 0;
    track.picture_format = PictureFormat::None;
    uint8_t head[10]{};
    bool valid = exact(0, head, sizeof(head));
    if (valid && !std::memcmp(head, "ID3", 3))
    {
        const uint8_t version = head[3], flags = head[5];
        uint32_t size = 0;
        if (version < 2 || version > 4 || !syncsafe(head + 6, size) || size > input.size() - 10) return false;
        uint32_t start = 10, end = 10 + size;
        track.audio_offset = end;
        // v2.4 size excludes both header and footer (ID3v2.4 structure 3.1).
        if (version == 4 && (flags & 0x10))
        {
            uint8_t footer[10];
            if (input.size() - end < 10 || !exact(end, footer, 10) || std::memcmp(footer, "3DI", 3) || std::memcmp(footer + 3, head + 3, 7)) return false;
            track.audio_offset += 10;
        }
        const bool unsupported = (version < 4 && (flags & 0x80)) || (version == 2 && (flags & 0x40));
        if (unsupported)
        {
            if (page) page->unsupported_flags = true;
        }
        else
        {
            if (version >= 3 && (flags & 0x40))
            {
                uint8_t extended[4];
                uint32_t length = 0;
                if (!exact(start, extended, 4)) return false;
                if (version == 4)
                {
                    if (!syncsafe(extended, length) || length < 6) return false;
                }
                else
                {
                    length = be32(extended);
                    if (length > UINT32_MAX - 4) return false;
                    length += 4;
                }
                if (length > end - start) return false;
                start += length;
            }
            for (unsigned frames = 0; frames < 512 && start < end; ++frames)
            {
                const uint32_t header_size = version == 2 ? 6 : 10;
                if (end - start < header_size || !exact(start, head, header_size)) break;
                if (!head[0]) break;
                char id[5]{};
                const unsigned id_size = version == 2 ? 3 : 4;
                for (unsigned n = 0; n < id_size; ++n)
                {
                    if (!((head[n] >= 'A' && head[n] <= 'Z') || (head[n] >= '0' && head[n] <= '9'))) return false;
                    id[n] = static_cast<char>(head[n]);
                }
                uint32_t length = version == 2 ? uint32_t(head[3]) << 16 | uint32_t(head[4]) << 8 | head[5] : be32(head + 4);
                if (version == 4 && !syncsafe(head + 4, length)) return false;
                start += header_size;
                if (!length || length > end - start) return false;
                uint32_t payload = start;
                const auto frame_end = start + length;
                const uint8_t format = version == 2 ? 0 : head[9];
                const bool compressed = version == 3 ? format & 0xc0 : format & 0x0c;
                if (compressed)
                {
                    if (page) page->unsupported_flags = true;
                    field(id, frame_name(id), "Compressed or encrypted frame");
                }
                else
                {
                    if ((version == 3 && (format & 0x20)) || (version == 4 && (format & 0x40))) ++payload;
                    if (version == 4 && (format & 0x01)) payload += 4;
                    if (payload < frame_end) parse_frame(id, payload, frame_end, version == 4 && ((flags & 0x80) || (format & 0x02)), track);
                }
                start = frame_end;
                if (frames == 511 && start < end && page) page->truncated = true;
            }
        }
    }
    // ID3v1 supplements missing common fields; it never replaces v2 text.
    if (input.size() >= 128 && exact(input.size() - 128, buffer_, 128) && !std::memcmp(buffer_, "TAG", 3))
    {
        const uint32_t base = input.size() - 128;
        const struct
        {
            unsigned at, length;
            const char* id;
            char* dest;
            std::size_t capacity;
        } tags[] = {
            {3, 30, "TIT2", track.title, sizeof(track.title)}, {33, 30, "TPE1", track.artist, sizeof(track.artist)}, {63, 30, "TALB", track.album, sizeof(track.album)}, {93, 4, "TYER", nullptr, 0}, {97, 28, "COMM", nullptr, 0}};
        for (const auto& tag : tags)
        {
            if (tag.dest && tag.dest[0]) continue;
            text(base + tag.at, base + tag.at + tag.length, 0, value_, sizeof(value_));
            if (tag.dest) copy(tag.dest, tag.capacity, value_);
            field(tag.id, frame_name(tag.id), value_);
        }
    }
    if (!track.title[0])
    {
        const auto* name = std::strrchr(track.path, '/');
        copy(track.title, sizeof(track.title), name ? name + 1 : track.path);
        auto* dot = std::strrchr(track.title, '.');
        if (dot) *dot = 0;
    }
    field("FILE", "File", track.path);
    if (page) page->total = fields_;
    return valid;
}
} // namespace music
