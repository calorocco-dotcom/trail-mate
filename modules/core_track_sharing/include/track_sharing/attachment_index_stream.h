#pragma once
#include "track_sharing/attachment_index.h"

namespace track_sharing
{
struct AttachmentFingerprint
{
    uint64_t id = 0, reference = 0;
};
enum class AttachmentStreamResult : uint8_t
{
    Pending,
    Entry,
    Parsed,
    Invalid
};
// Caller lends one entry window and transient fingerprint slots (typically
// PSRAM). Entry views are valid only until advance(). No index-sized buffer,
// owned strings, attachment table or hash context. Fingerprints conservatively
// reject collisions; they are never file hashes or author proofs.
class AttachmentIndexStream
{
  public:
    AttachmentIndexStream(uint8_t* window, size_t capacity, AttachmentFingerprint* fingerprints, size_t slots,
                          geocaching::ByteView kml_hash, uint32_t kml_bytes, uint32_t index_bytes, uint8_t count)
        : window_(window), capacity_(capacity), fingerprints_(fingerprints), kml_hash_(kml_hash),
          kml_bytes_(kml_bytes), index_bytes_(index_bytes), expected_count_(count)
    {
        if (!window || capacity < 4352 || !fingerprints || slots < kMaxTrackAttachments || !kml_hash.data || kml_hash.size != 32 ||
            !kml_bytes || kml_bytes > 8388608 || !index_bytes || index_bytes > kMaxAttachmentIndexBytes || !count || count > kMaxTrackAttachments ||
            signingSpansOverlap(window, capacity, reinterpret_cast<const uint8_t*>(fingerprints), kMaxTrackAttachments * sizeof(*fingerprints)) ||
            signingSpansOverlap(window, capacity, kml_hash.data, 32) ||
            signingSpansOverlap(kml_hash.data, 32, reinterpret_cast<const uint8_t*>(fingerprints), kMaxTrackAttachments * sizeof(*fingerprints)) ||
            signingSpansOverlap(window, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            signingSpansOverlap(reinterpret_cast<const uint8_t*>(fingerprints), kMaxTrackAttachments * sizeof(*fingerprints), reinterpret_cast<const uint8_t*>(this), sizeof(*this)))
            phase_ = Phase::Failed;
        total_ = static_cast<uint64_t>(index_bytes) + kml_bytes;
    }
    AttachmentStreamResult feed(const uint8_t* bytes, size_t size, size_t& consumed)
    {
        consumed = 0;
        if (phase_ == Phase::Failed) return AttachmentStreamResult::Invalid;
        if (phase_ == Phase::Entry) return AttachmentStreamResult::Entry;
        if ((!bytes && size) || size > index_bytes_ - received_ ||
            signingSpansOverlap(bytes, size, window_, capacity_) ||
            signingSpansOverlap(bytes, size, reinterpret_cast<const uint8_t*>(fingerprints_), kMaxTrackAttachments * sizeof(*fingerprints_))) return fail();
        if (phase_ == Phase::Complete) return size ? fail() : AttachmentStreamResult::Parsed;
        for (; consumed < size;)
        {
            if (used_ >= capacity_) return fail();
            const auto byte = bytes[consumed++];
            ++received_;
            window_[used_++] = byte;
            if (phase_ == Phase::Header)
            {
                if ((used_ == 1 && byte != 0x94) || (used_ == 2 && byte != 1) ||
                    (used_ == 3 && byte != 0xc4) || (used_ == 4 && byte != 32)) return fail();
                if (headerReady())
                {
                    phase_ = Phase::EntryTag;
                    used_ = 0;
                }
                else if (used_ >= 64) return fail();
                continue;
            }
            if (phase_ == Phase::EntryTag)
            {
                if (byte != 0x97) return fail();
                phase_ = Phase::FieldTag;
                field_ = 0;
            }
            else if (phase_ == Phase::FieldTag)
            {
                const bool text = field_ == 0 || field_ == 3 || field_ == 4;
                limit_ = field_ == 0 ? 48 : field_ == 3 ? 96
                                        : field_ == 4   ? 4096
                                                        : 32;
                if (text || field_ == 1)
                {
                    if (text && byte >= 0xa0 && byte <= 0xbf)
                    {
                        if (!payload(byte & 31)) return fail();
                    }
                    else if (text ? byte >= 0xd9 && byte <= 0xdb : byte >= 0xc4 && byte <= 0xc6)
                    {
                        length_bytes_ = static_cast<uint8_t>(1U << (byte - (text ? 0xd9 : 0xc4)));
                        length_ = 0;
                        phase_ = Phase::Length;
                    }
                    else return fail();
                }
                else if (byte <= 0x7f) completeField();
                else if (byte >= 0xcc && byte <= 0xcf)
                {
                    remaining_ = 1U << (byte - 0xcc);
                    phase_ = Phase::Payload;
                }
                else return fail();
            }
            else if (phase_ == Phase::Length)
            {
                length_ = (length_ << 8) | byte;
                if (!--length_bytes_ && !payload(length_)) return fail();
            }
            else if (phase_ == Phase::Payload)
            {
                if (!--remaining_) completeField();
            }
            if (phase_ == Phase::Decode)
            {
                if (!decodeAttachmentEntry({window_, used_}, entry_)) return fail();
                const auto id = fingerprint(entry_.id, true), reference = fingerprint(entry_.reference, false);
                for (unsigned i = 0; i < seen_; ++i)
                    if (fingerprints_[i].id == id || fingerprints_[i].reference == reference) return fail();
                fingerprints_[seen_] = {id, reference};
                total_ += entry_.bytes;
                if (total_ > kMaxTrackPackageBytes) return fail();
                ++seen_;
                if (entry_.required) ++required_;
                phase_ = Phase::Entry;
                return AttachmentStreamResult::Entry;
            }
        }
        return AttachmentStreamResult::Pending;
    }
    const AttachmentEntryView* entry() const { return phase_ == Phase::Entry ? &entry_ : nullptr; }
    bool admitted() const { return phase_ != Phase::Failed; }
    bool advance()
    {
        if (phase_ != Phase::Entry) return false;
        entry_ = {};
        used_ = 0;
        phase_ = seen_ == expected_count_ ? Phase::Complete : Phase::EntryTag;
        return true;
    }
    bool finish() const { return phase_ == Phase::Complete && received_ == index_bytes_ && seen_ == expected_count_ && total_ <= kMaxTrackPackageBytes; }
    uint32_t consumedBytes() const { return received_; }
    uint32_t packageBytes() const { return static_cast<uint32_t>(total_); }
    uint8_t requiredCount() const { return required_; }

  private:
    enum class Phase : uint8_t
    {
        Header,
        EntryTag,
        FieldTag,
        Length,
        Payload,
        Decode,
        Entry,
        Complete,
        Failed
    };
    bool headerReady()
    {
        geocaching::protocol::CmpReader reader({window_, used_});
        size_t fields = 0, count = 0;
        uint64_t version = 0, bytes = 0;
        geocaching::ByteView kml;
        return reader.array(fields, 4) && fields == 4 && reader.unsignedInteger(version) && version == 1 &&
               reader.binary(kml, 32) && kml.size == 32 && reader.unsignedInteger(bytes) && bytes == kml_bytes_ &&
               reader.array(count, kMaxTrackAttachments) && count == expected_count_ && reader.finished() && !std::memcmp(kml.data, kml_hash_.data, 32);
    }
    bool payload(uint32_t bytes)
    {
        if (bytes > limit_ || bytes > capacity_ - used_) return false;
        remaining_ = bytes;
        if (!bytes) completeField();
        else phase_ = Phase::Payload;
        return true;
    }
    void completeField() { phase_ = ++field_ == 7 ? Phase::Decode : Phase::FieldTag; }
    AttachmentStreamResult fail()
    {
        phase_ = Phase::Failed;
        entry_ = {};
        return AttachmentStreamResult::Invalid;
    }
    static uint64_t fingerprint(std::string_view value, bool folded)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (char c : value) hash = (hash ^ static_cast<uint8_t>(folded ? attachmentLower(c) : c)) * 1099511628211ULL;
        return hash;
    }
    uint8_t* window_;
    size_t capacity_, used_ = 0;
    AttachmentFingerprint* fingerprints_;
    geocaching::ByteView kml_hash_;
    AttachmentEntryView entry_;
    uint64_t total_ = 0;
    uint32_t kml_bytes_, index_bytes_, received_ = 0, remaining_ = 0, length_ = 0, limit_ = 0;
    uint8_t expected_count_, seen_ = 0, required_ = 0, field_ = 0, length_bytes_ = 0;
    Phase phase_ = Phase::Header;
};
static_assert(sizeof(AttachmentIndexStream) <= 208, "Stream owns only control state and a borrowed entry view");
} // namespace track_sharing
