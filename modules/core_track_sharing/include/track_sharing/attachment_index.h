#pragma once
#include "track_sharing/manifest.h"
#include "track_sharing/sign_manifest.h"

namespace track_sharing
{
struct AttachmentEntryView
{
    std::string_view id, mime, reference;
    geocaching::ByteView hash;
    uint32_t bytes = 0;
    uint8_t role = 0;
    bool required = false;
};
static_assert(sizeof(AttachmentEntryView) <= 80, "Attachment entry borrows all strings");
inline char attachmentLower(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
inline bool attachmentIdsEqual(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (attachmentLower(a[i]) != attachmentLower(b[i])) return false;
    return true;
}
inline bool attachmentMimeValid(std::string_view mime)
{
    const auto slash = mime.find('/');
    if (!slash || slash == mime.npos || slash + 1 == mime.size()) return false;
    for (size_t i = 0; i < mime.size(); ++i)
    {
        const char c = attachmentLower(mime[i]);
        if (i == slash) continue;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '!' || c == '#' || c == '$' || c == '&' || c == '^' || c == '_' || c == '.' || c == '+' || c == '-')) return false;
    }
    return true;
}
inline bool attachmentReferenceValid(std::string_view reference)
{
    if (!geocaching::protocol::validRecordText(reference, false, true)) return false;
    const bool http = reference.size() >= 7 && attachmentIdsEqual(reference.substr(0, 7), "http://");
    const bool https = reference.size() >= 8 && attachmentIdsEqual(reference.substr(0, 8), "https://");
    if (http || https)
    {
        const auto authority = reference.substr(https ? 8 : 7);
        const auto end = authority.find_first_of("/?#");
        const auto host = authority.substr(0, end);
        if (host.empty() || host.find('@') != host.npos) return false;
        for (const auto c : reference)
            if (c == '\\' || static_cast<unsigned char>(c) <= 32) return false;
        return true; // exact reference preserved; never fetched by this decoder
    }
    const auto end = reference.find_first_of("?#");
    const auto path = reference.substr(0, end);
    if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
    size_t component_size = 0;
    bool dots_only = true;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/')
        {
            if (!component_size || (dots_only && component_size <= 2)) return false;
            component_size = 0;
            dots_only = true;
            continue;
        }
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (c == '%')
        {
            const auto digit = [](char value) -> int
            { value = attachmentLower(value); return value >= '0' && value <= '9' ? value - '0' : value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1; };
            if (i + 2 >= path.size() || digit(path[i + 1]) < 0 || digit(path[i + 2]) < 0) return false;
            c = static_cast<unsigned char>(digit(path[i + 1]) * 16 + digit(path[i + 2]));
            i += 2;
            if (c == '/') return false;
        }
        if (c == '\\' || c == ':' || c < 32 || c == 127) return false;
        ++component_size;
        if (c != '.') dots_only = false;
    }
    return true;
}
inline bool readAttachmentEntry(geocaching::protocol::CmpReader& reader, AttachmentEntryView& output)
{
    output = {};
    size_t fields = 0;
    uint64_t value = 0;
    if (!reader.array(fields, 7) || fields != 7 || !reader.text(output.id, 48) || output.id.empty()) return false;
    for (const auto c : output.id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    if (!reader.binary(output.hash, 32) || output.hash.size != 32 || !reader.unsignedInteger(value) || !value || value > 8388608) return false;
    output.bytes = static_cast<uint32_t>(value);
    if (!reader.text(output.mime, 96) || !attachmentMimeValid(output.mime) || !reader.text(output.reference, 4096) || !attachmentReferenceValid(output.reference) ||
        !reader.unsignedInteger(value) || value > 3) return false;
    output.role = static_cast<uint8_t>(value);
    if ((output.role != 3 || (output.mime.size() >= 6 && attachmentIdsEqual(output.mime.substr(0, 6), "image/"))) && output.bytes > 4194304) return false;
    if (!reader.unsignedInteger(value) || value > 1) return false;
    output.required = value == 1; // CMP1 uses constrained u8, not MessagePack bool
    return true;
}
inline bool decodeAttachmentEntry(geocaching::ByteView raw, AttachmentEntryView& output)
{
    geocaching::protocol::CmpReader reader(raw);
    return raw.data && raw.size && raw.size <= 4352 && readAttachmentEntry(reader, output) && reader.finished();
}

// Borrowed immutable backing (e.g. admitted PSRAM). Cursor retains no entry
// list; duplicate checking scans earlier entries rather than copying strings.
// File-stream integration must lend bounded entry windows under the same lease.
class AttachmentIndexCursor
{
  public:
    explicit AttachmentIndexCursor(geocaching::ByteView raw) : reader_(geocaching::ByteView{}) { begin(raw); }
    bool begin(geocaching::ByteView raw)
    {
        raw_ = raw;
        reader_ = geocaching::protocol::CmpReader(raw);
        kml_hash_ = {};
        entries_start_ = count_ = seen_ = required_ = 0;
        total_ = kml_bytes_ = 0;
        size_t fields = 0;
        uint64_t value = 0;
        valid_ = raw.data && raw.size && raw.size <= kMaxAttachmentIndexBytes && reader_.array(fields, 4) && fields == 4 &&
                 reader_.unsignedInteger(value) && value == 1 && reader_.binary(kml_hash_, 32) && kml_hash_.size == 32 &&
                 reader_.unsignedInteger(value) && value && value <= 8388608;
        if (!valid_) return false;
        kml_bytes_ = static_cast<uint32_t>(value);
        valid_ = reader_.array(count_, kMaxTrackAttachments) && count_ > 0;
        entries_start_ = reader_.position();
        total_ = raw.size + kml_bytes_;
        return valid_;
    }
    bool next(AttachmentEntryView& output)
    {
        output = {};
        if (!valid_ || seen_ >= count_) return false;
        if (!readAttachmentEntry(reader_, output)) return fail();
        geocaching::protocol::CmpReader earlier({raw_.data + entries_start_, raw_.size - entries_start_});
        AttachmentEntryView previous;
        for (size_t i = 0; i < seen_; ++i)
            if (!readAttachmentEntry(earlier, previous) || attachmentIdsEqual(previous.id, output.id) || previous.reference == output.reference) return fail();
        total_ += output.bytes;
        if (total_ > kMaxTrackPackageBytes) return fail();
        ++seen_;
        if (output.required) ++required_;
        return true;
    }
    bool finish() const { return valid_ && seen_ == count_ && reader_.finished() && total_ <= kMaxTrackPackageBytes; }
    geocaching::ByteView kmlHash() const { return kml_hash_; }
    uint32_t kmlBytes() const { return kml_bytes_; }
    size_t count() const { return count_; }
    size_t requiredCount() const { return required_; }
    uint32_t packageBytes() const { return static_cast<uint32_t>(total_); }

  private:
    bool fail()
    {
        valid_ = false;
        return false;
    }
    geocaching::ByteView raw_, kml_hash_;
    geocaching::protocol::CmpReader reader_;
    size_t entries_start_ = 0, count_ = 0, seen_ = 0, required_ = 0;
    uint64_t total_ = 0;
    uint32_t kml_bytes_ = 0;
    bool valid_ = false;
};
static_assert(sizeof(AttachmentIndexCursor) <= 128, "Index cursor owns no entries or payload buffers");
// Caller lends hash output, cursor and entry scratch; no large automatic views.
// Binding validity is not author proof, resource availability or install proof.
template <class Hash>
bool validateAttachmentIndexBinding(geocaching::ByteView raw, const AttachmentIndexBinding& binding,
                                    geocaching::ByteView kml_hash, uint32_t kml_bytes, Hash& hash, uint8_t digest[32],
                                    AttachmentIndexCursor& cursor, AttachmentEntryView& entry)
{
    if (!raw.data || !digest || !binding.hash.data || binding.hash.size != 32 || !kml_hash.data || kml_hash.size != 32 ||
        binding.bytes != raw.size || !binding.count || binding.count > kMaxTrackAttachments ||
        signingSpansOverlap(raw.data, raw.size, digest, 32) || signingSpansOverlap(binding.hash.data, 32, digest, 32) ||
        signingSpansOverlap(kml_hash.data, 32, digest, 32) ||
        signingSpansOverlap(raw.data, raw.size, reinterpret_cast<const uint8_t*>(&cursor), sizeof(cursor)) ||
        signingSpansOverlap(raw.data, raw.size, reinterpret_cast<const uint8_t*>(&entry), sizeof(entry)) ||
        signingSpansOverlap(digest, 32, reinterpret_cast<const uint8_t*>(&cursor), sizeof(cursor)) ||
        signingSpansOverlap(digest, 32, reinterpret_cast<const uint8_t*>(&entry), sizeof(entry))) return false;
    std::memset(digest, 0, 32);
    if (!cursor.begin(raw) || cursor.count() != binding.count || cursor.kmlBytes() != kml_bytes ||
        std::memcmp(cursor.kmlHash().data, kml_hash.data, 32)) return false;
    while (cursor.next(entry))
    {
    }
    if (!cursor.finish()) return false;
    const bool ok = hash.start() && hash.update(raw.data, raw.size) && hash.finish(digest);
    const bool matched = ok && !std::memcmp(binding.hash.data, digest, 32);
    hash.reset();
    if (!matched)
    {
        std::memset(digest, 0, 32);
        return false;
    }
    return true;
}
} // namespace track_sharing
