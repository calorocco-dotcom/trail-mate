#pragma once
#include "platform/esp/idf_common/hostlink_track_history_authentication.h"
#include "platform/esp/idf_common/hostlink_track_history_scan.h"
namespace hostlink::management::publication_runtime
{
// Borrowed scanner/reader and owned transient authentication. Caller owns the
// exclusive SD lease and allocates all backing objects in admitted PSRAM.
template <class Scan, class Reader>
class HistoryQuery
{
  public:
    HistoryQuery(Scan& scan, Reader& reader, geocaching::ByteView operation, uint32_t generation, uint32_t media)
        : scan_(scan), reader_(reader), generation_(generation), media_(media)
    {
        if (operation.data && operation.size == 16 && generation)
        {
            uint8_t bits = 0;
            for (size_t i = 0; i < 16; ++i) bits |= operation.data[i];
            if (bits)
            {
                std::memcpy(operation_, operation.data, 16);
                status_ = Status::Busy;
            }
        }
    }
    ~HistoryQuery() { destroyPublicationAdmission(authentication_); }
    HistoryQuery(const HistoryQuery&) = delete;
    HistoryQuery& operator=(const HistoryQuery&) = delete;
    Status step(chat::IMeshAdapter& adapter, uint8_t* scratch, size_t capacity)
    {
        if (status_ != Status::Busy && status_ != Status::Ok) return status_;
        if (!scratch || capacity < 448) return fail(Status::InvalidField);
        if (media_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return fail(Status::StorageUnavailable);
        if (authentication_)
        {
            const auto result = authentication_->step(adapter, scratch, capacity);
            if (result == Status::Busy) return result;
            if (result != Status::Ok) return fail(result);
            return status_ = Status::Ok;
        }
        if (!selected_)
        {
            const auto scanned = scan_.step(key_);
            if (scanned == HistoryScanResult::Pending || scanned == HistoryScanResult::Busy) return Status::Busy;
            if (scanned == HistoryScanResult::End) return fail(Status::Expired);
            if (scanned != HistoryScanResult::Candidate) return fail(scanned == HistoryScanResult::Unavailable ? Status::StorageUnavailable : Status::Conflict);
            if (std::memcmp(operation_, key_.operation, 16)) return Status::Busy;
            selected_ = true;
        }
        if (!candidate_loaded_)
        {
            const auto loaded = reader_.load(key_, scratch, capacity);
            if (loaded != track_sharing::PublicationStoreResult::Ready) return fail(loaded == track_sharing::PublicationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
            candidate_loaded_ = true;
        }
        const auto* job = reader_.unverifiedJournal();
        const auto* directory = reader_.unverifiedDirectory();
        if (!job || !directory) return fail(Status::Conflict);
        if (job->generation != generation_) return fail(Status::StaleIdentity);
        const auto started = beginHistoryAuthentication(adapter, *job, *directory, media_, scratch, capacity, authentication_);
        if (started != Status::Busy) return fail(started);
        return Status::Busy;
    }
    const track_sharing::PublicationJournal* result() const { return status_ == Status::Ok && authentication_ ? authentication_->result() : nullptr; }

  private:
    Status fail(Status result)
    {
        destroyPublicationAdmission(authentication_);
        authentication_ = nullptr;
        return status_ = result;
    }
    Scan& scan_;
    Reader& reader_;
    TrackPublicationAdmission* authentication_ = nullptr;
    track_sharing::PublicationSlotKey key_;
    uint8_t operation_[16]{};
    uint32_t generation_, media_;
    Status status_ = Status::InvalidField;
    bool selected_ = false, candidate_loaded_ = false;
};
} // namespace hostlink::management::publication_runtime
