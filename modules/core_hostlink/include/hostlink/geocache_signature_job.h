#pragma once
#include "geocaching/usecase/author_issue.h"
#include <cstdint>
#include <cstring>

namespace hostlink::management
{
// Request owner pins the record and output work area in PSRAM. This job only
// reserves/signs; neither an RF adapter nor a network publication is involved.
class GeocacheSignatureJob
{
  public:
    explicit GeocacheSignatureJob(geocaching::AuthorIssuePort& port) : issue_(port) {}
    bool begin(geocaching::ByteView record, uint8_t* work, size_t capacity,
               const uint8_t author[64], uint32_t generation)
    {
        if (started_ || !author || !generation) return false;
        geocaching::RecordView decoded;
        if (!geocaching::protocol::decodeGeocacheRecord(record, decoded) ||
            std::memcmp(decoded.author_public_key.data, author, 64)) return false;
        if (!issue_.begin(record, work, capacity)) return false;
        std::memcpy(author_, author, 64);
        generation_ = generation;
        started_ = true;
        return true;
    }
    void advance(const uint8_t author[64], uint32_t generation, bool owner_valid)
    {
        if (!authorized(author, generation, owner_valid))
        {
            invalidate();
            return;
        }
        issue_.advance();
    }
    geocaching::AuthorIssuePhase phase() const
    {
        return invalidated_ ? geocaching::AuthorIssuePhase::Cancelled : issue_.phase();
    }
    // Borrow at most one HostLink transfer slice. The command must encode it
    // synchronously; no pointer may survive the owner lease or USB disconnect.
    bool read(size_t offset, size_t maximum, const uint8_t author[64], uint32_t generation,
              bool owner_valid, geocaching::ByteView& chunk, size_t& total)
    {
        chunk = {};
        total = 0;
        if (!authorized(author, generation, owner_valid))
        {
            invalidate();
            return false;
        }
        const auto signed_record = issue_.signedRecord();
        if (!signed_record.data || !signed_record.size || !maximum || maximum > 320 || offset >= signed_record.size) return false;
        const auto remaining = signed_record.size - offset;
        chunk = {signed_record.data + offset, remaining < maximum ? remaining : maximum};
        total = signed_record.size;
        return true;
    }
    void invalidate()
    {
        issue_.cancel();
        invalidated_ = true;
    }

  private:
    bool authorized(const uint8_t* author, uint32_t generation, bool valid) const
    {
        return started_ && !invalidated_ && valid && author && generation == generation_ &&
               !std::memcmp(author_, author, 64);
    }
    geocaching::AuthorIssue issue_;
    uint8_t author_[64]{};
    uint32_t generation_ = 0;
    bool started_ = false, invalidated_ = false;
};
static_assert(sizeof(GeocacheSignatureJob) <= 256, "Signature jobs borrow payload/work storage");
} // namespace hostlink::management
