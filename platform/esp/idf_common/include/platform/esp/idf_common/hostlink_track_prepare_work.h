#pragma once
#include "platform/esp/idf_common/hostlink_prepare_scheduler_work.h"
#include "platform/esp/idf_common/hostlink_track_attachment_retention.h"
#include "platform/esp/idf_common/hostlink_track_kml_retention.h"
#include "platform/esp/idf_common/hostlink_track_prepare_persistence.h"
#include "platform/esp/idf_common/hostlink_track_retention_admission.h"
namespace hostlink::management::prepare_runtime
{
// One scheduler-owned transient PSRAM job. Owns input after successful creation;
// big stages release immediately on terminal outcome, metadata expires in 30s.
class TrackPrepareWork : public PrepareSchedulerWork
{
  public:
    TrackPrepareWork(TrackPrepareOwner* input, const uint8_t* session, uint64_t now)
        : input_(input), media_(input->media), generation_(input->generation), last_(now)
    {
        std::memcpy(session_, session, 16);
        std::memcpy(author_hash_, input->author_hash, 32);
        std::memcpy(upload_ids_, input->request.kml_upload.data, 16);
        std::memcpy(upload_ids_ + 16, input->request.manifest_upload.data, 16);
        package_request_ = input->request.package_request;
        package_present_ = input->request.package_upload.size == 16;
        if (package_present_) std::memcpy(upload_ids_ + 32, input->request.package_upload.data, 16);
        std::memcpy(result_.operation, input->request.operation.data, 16);
        parent_present_ = input->request.expected_parent.size == 32;
        if (parent_present_) std::memcpy(result_.parent, input->request.expected_parent.data, 32);
    }
    ~TrackPrepareWork() { releaseStages(); }
    TrackPrepareWork(const TrackPrepareWork&) = delete;
    TrackPrepareWork& operator=(const TrackPrepareWork&) = delete;
    bool alive(uint32_t media, uint64_t now, bool connected) const { return connected && media == media_ && now >= last_ && now - last_ < 30000; }
    bool matchesSession(const uint8_t* session) const { return session && !std::memcmp(session, session_, 16); }
    bool holdsStages() const { return input_ || persistence_; }
    void suspendIdentity() { identity_verified_ = false; }
    geocaching::ByteView operationId() const { return {result_.operation, 16}; }
    bool renewMatched(uint64_t now)
    {
        if (!identity_verified_ || !alive(sd::sd_media_session(), now, true)) return false;
        last_ = now;
        return true;
    }
    bool matches(const uint8_t* session, const track_sharing::PrepareRequest& request) const
    {
        return session && !std::memcmp(session, session_, 16) && request.generation == generation_ &&
               !request.install_request &&
               request.operation.data && request.operation.size == 16 && !std::memcmp(request.operation.data, result_.operation, 16) &&
               request.kml_upload.data && request.kml_upload.size == 16 && !std::memcmp(request.kml_upload.data, upload_ids_, 16) &&
               request.manifest_upload.data && request.manifest_upload.size == 16 && !std::memcmp(request.manifest_upload.data, upload_ids_ + 16, 16) &&
               request.package_request == package_request_ &&
               (package_present_ ? request.package_upload.data && request.package_upload.size == 16 && !std::memcmp(request.package_upload.data, upload_ids_ + 32, 16) : !request.package_upload.size) &&
               (parent_present_ ? request.expected_parent.data && request.expected_parent.size == 32 && !std::memcmp(request.expected_parent.data, result_.parent, 32) : request.expected_parent.size == 0);
    }
    bool authorize(const uint8_t* session, const track_sharing::PrepareRequest& request, uint64_t now)
    {
        if (!alive(sd::sd_media_session(), now, true) || !matches(session, request)) return false;
        last_ = now;
        return true;
    }
    Status advance(chat::IMeshAdapter& adapter, uint8_t* scratch, size_t capacity)
    {
        identity_verified_ = false;
        if (!scratch || capacity < 448) return fail(Status::Internal);
        if (media_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return fail(Status::StorageUnavailable);
        uint8_t hash[32];
        uint32_t generation = 0;
        const auto identity = upload_runtime::readUploadAuthor(adapter, scratch + capacity - 64, 64, hash, generation);
        if (identity == Status::Busy) return Status::Busy;
        if (identity != Status::Ok) return fail(identity);
        if (generation != generation_ || std::memcmp(hash, author_hash_, 32)) return fail(Status::StaleIdentity);
        identity_verified_ = true;
        if (terminal_) return status_;
        if (attachment_retention_) return advanceAttachments(scratch, capacity);
        if (retention_) return advanceRetention(scratch, capacity);
        if (!input_->ready())
        {
            const auto checked = input_->step(generation, hash, scratch, capacity);
            if (checked == track_sharing::ManifestFileResult::Pending) return Status::Busy;
            if (checked != track_sharing::ManifestFileResult::Ready) return fail(checked == track_sharing::ManifestFileResult::StorageFailure ? Status::StorageUnavailable : checked == track_sharing::ManifestFileResult::HashMismatch ? Status::HashMismatch
                                                                                                                                                                                                                                        : Status::InvalidField);
        }
        if (!persistence_)
        {
            persistence_ = createPreparePersistence(*input_);
            if (!persistence_) return Status::Busy;
        }
        const auto finished = persistence_->finish(adapter, nullptr, scratch, capacity);
        if (finished == Status::Busy) return finished;
        if (finished != Status::Ok) return fail(finished);
        const auto& head = persistence_->head();
        std::memcpy(result_.object, head.object, 32);
        std::memcpy(result_.hash, head.hash, 32);
        std::memcpy(result_.parent, head.parent, 32);
        std::memcpy(result_.operation, head.operation, 16);
        result_.revision = head.revision;
        const auto signed_bytes = persistence_->signature();
        if (signed_bytes.size != sizeof(signature_)) return fail(Status::Internal);
        std::memcpy(signature_, signed_bytes.data, sizeof(signature_));
        const auto admitted = input_->view.version == 2 ? admitRetainedPackageFile(media_, {result_.object, 32}, input_->view.file_hash, input_->view.file_bytes,
                                                                                   track_sharing::ObjectFile::KmlFile, track_sharing::ObjectFile::KmlTemporary, scratch, capacity)
                                                        : admitRetainedKml(media_, {result_.object, 32}, input_->view.file_hash, input_->view.file_bytes, scratch, capacity);
        if (admitted == Status::Busy) return Status::Busy;
        if (admitted != Status::Ok) return fail(admitted);
        void* memory = heap_caps_malloc(sizeof(TrackKmlRetention), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return Status::Busy;
        retention_ = new (memory) TrackKmlRetention(*input_);
        const auto started = retention_->begin({result_.object, 32});
        if (started != track_sharing::ImmutableCopyResult::Pending) return fail(started == track_sharing::ImmutableCopyResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        return Status::Busy;
    }
    bool ready() const { return identity_verified_ && terminal_ && status_ == Status::Ok && media_ == sd::sd_media_session() && sd::sd_card_ready(); }
    const TrackPreparedResult* result() const { return ready() ? &result_ : nullptr; }
    geocaching::ByteView preparedSignature() const { return ready() ? geocaching::ByteView{signature_, sizeof(signature_)} : geocaching::ByteView{}; }
    Status status() const { return terminal_ ? status_ : Status::Busy; }

  private:
    Status advanceRetention(uint8_t* scratch, size_t capacity)
    {
        const auto retained = retention_->step(scratch, capacity);
        if (retained == track_sharing::ImmutableCopyResult::Pending) return Status::Busy;
        if (retained != track_sharing::ImmutableCopyResult::Ready)
            return fail(retained == track_sharing::ImmutableCopyResult::HashMismatch ? Status::HashMismatch : retained == track_sharing::ImmutableCopyResult::StorageFailure ? Status::StorageUnavailable
                                                                                                                                                                             : Status::Conflict);
        if (input_->attachments)
        {
            void* memory = heap_caps_malloc(sizeof(TrackAttachmentRetention), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!memory) return Status::Busy;
            attachment_retention_ = new (memory) TrackAttachmentRetention(*input_->attachments, {result_.object, 32}, media_);
            return Status::Busy;
        }
        terminal_ = true;
        status_ = Status::Ok;
        releaseStages();
        return status_;
    }
    Status advanceAttachments(uint8_t* scratch, size_t capacity)
    {
        const auto retained = attachment_retention_->step(scratch, capacity);
        if (retained == Status::Busy) return retained;
        if (retained != Status::Ok) return fail(retained);
        terminal_ = true;
        status_ = Status::Ok;
        releaseStages();
        return status_;
    }
    Status fail(Status status)
    {
        terminal_ = true;
        status_ = status;
        releaseStages();
        return status;
    }
    void releaseStages()
    {
        if (attachment_retention_)
        {
            attachment_retention_->~TrackAttachmentRetention();
            heap_caps_free(attachment_retention_);
            attachment_retention_ = nullptr;
        }
        if (retention_)
        {
            retention_->~TrackKmlRetention();
            heap_caps_free(retention_);
            retention_ = nullptr;
        }
        destroyPreparePersistence(persistence_);
        persistence_ = nullptr;
        destroyPrepareOwner(input_);
        input_ = nullptr;
    }
    TrackPrepareOwner* input_;
    TrackPreparePersistence* persistence_ = nullptr;
    TrackKmlRetention* retention_ = nullptr;
    TrackAttachmentRetention* attachment_retention_ = nullptr;
    uint8_t session_[16]{}, author_hash_[32]{}, upload_ids_[48]{};
    TrackPreparedResult result_;
    uint8_t signature_[64]{};
    uint32_t media_, generation_;
    uint64_t last_;
    Status status_ = Status::Busy;
    bool parent_present_ = false, terminal_ = false, identity_verified_ = false;
    bool package_request_ = false, package_present_ = false;
};
// Four borrowed stage pointers and the scheduler's virtual pointer grow on a
// 64-bit host, with one extra alignment gap before last_. The ESP budget stays
// exactly 320 bytes; this check does not permit an owned response/file buffer.
static_assert(sizeof(void*) != 4 || sizeof(TrackPrepareWork) <= 320, "ESP prepare metadata exceeded its budget");
static_assert(sizeof(TrackPrepareWork) <= 320 + 5 * (sizeof(void*) - 4) + (sizeof(void*) > 4 ? 4 : 0),
              "Prepare result metadata cannot retain big buffers");
inline TrackPrepareWork* createPrepareWork(TrackPrepareOwner* input, const uint8_t* session, uint64_t now)
{
    if (!input || !session) return nullptr;
    uint8_t bits = 0;
    for (unsigned i = 0; i < 16; ++i) bits |= session[i];
    if (!bits) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackPrepareWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? new (memory) TrackPrepareWork(input, session, now) : nullptr;
}
inline void destroyPrepareWork(TrackPrepareWork* work)
{
    if (!work) return;
    work->~TrackPrepareWork();
    heap_caps_free(work);
}
} // namespace hostlink::management::prepare_runtime
