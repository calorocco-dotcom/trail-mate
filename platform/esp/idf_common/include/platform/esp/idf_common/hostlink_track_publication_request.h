#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/idf_common/hostlink_track_publication_admission.h"
#include "platform/esp/idf_common/hostlink_track_publication_execution.h"
#include "track_sharing/network_request.h"

namespace hostlink::management::publication_runtime
{
// Caller owns one scheduler slot, outside CDC transport lifetime. Large
// admission buffers retire immediately at completion; only metadata remains.
class TrackPublicationRequestWork
{
  public:
    TrackPublicationRequestWork(TrackPublicationAdmission* admission, const track_sharing::CommitRequest& request,
                                const track_sharing::PublicationDirectory& directory, const uint8_t* session,
                                const uint8_t* author_hash, uint32_t media, uint64_t now, bool retain = false)
        : admission_(admission), directory_(directory), media_(media), last_(now), retain_(retain)
    {
        std::memcpy(session_, session, 16);
        std::memcpy(job_.operation, request.operation.data, 16);
        std::memcpy(job_.object, request.object.data, 32);
        std::memcpy(job_.revision_hash, request.revision_hash.data, 32);
        std::memcpy(job_.directory, request.directory.data, 16);
        std::memcpy(job_.directory_epoch, request.directory_epoch.data, 16);
        std::memcpy(job_.author_hash, author_hash, 32);
        job_.generation = request.generation;
        job_.revision = request.revision;
    }
    ~TrackPublicationRequestWork() { destroyPublicationAdmission(admission_); }
    TrackPublicationRequestWork(const TrackPublicationRequestWork&) = delete;
    TrackPublicationRequestWork& operator=(const TrackPublicationRequestWork&) = delete;
    bool alive(uint64_t now, bool connected) const { return connected && media_ == sd::sd_media_session() && now >= last_ && now - last_ < 30000; }
    bool matchesSession(const uint8_t* session) const { return session && !std::memcmp(session, session_, 16); }
    bool matches(const uint8_t* session, const track_sharing::CommitRequest& request, const track_sharing::PublicationDirectory& directory) const
    {
        if (!matchesSession(session) || request.generation != job_.generation || request.revision != job_.revision ||
            !track_sharing::publicationDirectoriesEqual(directory_, directory)) return false;
        const geocaching::ByteView source[] = {request.operation, request.object, request.revision_hash, request.directory, request.directory_epoch};
        const geocaching::ByteView expected[] = {{job_.operation, 16}, {job_.object, 32}, {job_.revision_hash, 32}, {job_.directory, 16}, {job_.directory_epoch, 16}};
        for (size_t i = 0; i < 5; ++i)
            if (!source[i].data || source[i].size != expected[i].size || std::memcmp(source[i].data, expected[i].data, expected[i].size)) return false;
        return true;
    }
    bool renew(uint64_t now)
    {
        if (!alive(now, true)) return false;
        last_ = now;
        return true;
    }
    bool holdsAdmission() const { return admission_ != nullptr; }
    TrackPublicationExecution* startExecution(chat::IMeshAdapter& adapter, const uint8_t* local, chat::TrackBindingChange& changed)
    {
        if (status_ != Status::Ok || !identity_verified_ || !admission_ || !admission_->ready())
        {
            changed = chat::TrackBindingChange::Conflict;
            return nullptr;
        }
        return TrackPublicationExecution::create(admission_, adapter, local, changed);
    }
    void suspendIdentity() { identity_verified_ = false; }
    const track_sharing::PublicationJournal* result() const { return status_ == Status::Ok && identity_verified_ && media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active() ? &job_ : nullptr; }
    Status step(chat::IMeshAdapter& adapter, uint8_t* scratch, size_t capacity)
    {
        identity_verified_ = false;
        if (!scratch || capacity < 448 || track_sharing::signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this))) return Status::InvalidField;
        if (media_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return retire(Status::StorageUnavailable);
        if (!admission_)
        {
            if (status_ != Status::Ok) return status_;
            if (!scratch || capacity < 448) return Status::InvalidField;
            uint8_t hash[32];
            uint32_t generation = 0;
            const auto identity = upload_runtime::readUploadAuthor(adapter, scratch + capacity - 64, 64, hash, generation);
            if (identity != Status::Ok) return identity;
            if (generation != job_.generation || std::memcmp(hash, job_.author_hash, 32)) return retire(Status::StaleIdentity);
            identity_verified_ = true;
            return Status::Ok;
        }
        const auto advanced = admission_->step(adapter, scratch, capacity);
        if (advanced == Status::Busy) return advanced;
        if (advanced == Status::Ok && admission_->result())
        {
            job_ = *admission_->result();
            identity_verified_ = true;
            if (retain_) return status_ = Status::Ok;
        }
        return retire(advanced);
    }

  private:
    Status retire(Status status)
    {
        destroyPublicationAdmission(admission_);
        admission_ = nullptr;
        return status_ = status;
    }
    TrackPublicationAdmission* admission_;
    track_sharing::PublicationDirectory directory_;
    track_sharing::PublicationJournal job_;
    uint8_t session_[16]{};
    uint32_t media_;
    uint64_t last_;
    Status status_ = Status::Busy;
    bool identity_verified_ = false;
    bool retain_ = false;
};
static_assert(sizeof(TrackPublicationRequestWork) <= 384, "Completed admission retains metadata only");
inline void releasePublicationRequest(TrackPublicationRequestWork*& work)
{
    if (!work) return;
    work->~TrackPublicationRequestWork();
    heap_caps_free(work);
    work = nullptr;
}
inline Status advancePublicationRequest(TrackPublicationRequestWork*& work, chat::IMeshAdapter* adapter,
                                        uint8_t* scratch, size_t capacity, uint64_t now, bool connected)
{
    if (!work) return Status::Expired;
    if (!work->alive(now, connected))
    {
        releasePublicationRequest(work);
        return Status::Expired;
    }
    if (!adapter)
    {
        work->suspendIdentity();
        return Status::Busy;
    }
    return work->step(*adapter, scratch, capacity);
}
// Internal local-admission handler. Caller must obtain directory from pin(),
// hold exclusive storage and register durable network work before advertising
// operation 83. This function does not enable capabilities or send radio data.
inline Status processPublicationRequest(chat::IMeshAdapter& adapter, TrackPublicationRequestWork*& work,
                                        const EnvelopeView& request, const track_sharing::PublicationDirectory& directory,
                                        uint64_t now, uint8_t* scratch, size_t capacity, size_t& written, bool retain = false)
{
    written = 0;
    if (request.code != uint16_t(Operation::TrackCommit) || !request.session || !scratch || capacity < 448 ||
        !track_sharing::networkRequestId({request.session, 16})) return Status::InvalidField;
    track_sharing::CommitRequest decoded;
    if (!track_sharing::decodeCommitRequest({request.body, request.body_size}, decoded)) return Status::InvalidField;
    if (!track_sharing::publicationDirectoryValid(directory) || std::memcmp(directory.delivery, decoded.directory.data, 16) ||
        std::memcmp(directory.epoch, decoded.directory_epoch.data, 16)) return Status::InvalidField;
    auto* key = scratch + capacity - 64;
    if (track_sharing::signingSpansOverlap(request.body, request.body_size, key, 64) ||
        track_sharing::signingSpansOverlap(request.session, 16, key, 64) ||
        track_sharing::signingSpansOverlap(reinterpret_cast<const uint8_t*>(&directory), sizeof(directory), scratch, capacity)) return Status::InvalidField;
    if (work && track_sharing::signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(work), sizeof(*work))) return Status::InvalidField;
    if (work) work->suspendIdentity();
    uint8_t author_hash[32];
    uint32_t generation = 0;
    const auto identity = upload_runtime::readUploadAuthor(adapter, key, 64, author_hash, generation);
    if (identity != Status::Ok) return identity;
    if (generation != decoded.generation) return Status::StaleIdentity;
    if (work && !work->alive(now, true)) releasePublicationRequest(work);
    if (work && !work->matches(request.session, decoded, directory)) return Status::Conflict;
    if (!work)
    {
        auto* admission = createPublicationAdmission(decoded, directory, sd::sd_media_session(), key, author_hash);
        if (!admission) return Status::Busy;
        void* memory = heap_caps_malloc(sizeof(TrackPublicationRequestWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory)
        {
            destroyPublicationAdmission(admission);
            return Status::Busy;
        }
        work = new (memory) TrackPublicationRequestWork(admission, decoded, directory, request.session, author_hash, sd::sd_media_session(), now, retain);
    }
    const auto advanced = work->step(adapter, scratch, capacity);
    if (advanced != Status::Ok && advanced != Status::Busy) return advanced;
    if (!work->renew(now)) return Status::Expired;
    if (advanced == Status::Busy) return advanced;
    const auto* result = work->result();
    if (!result) return Status::Internal;
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    if (!writer.array(8) || !writer.binary({result->operation, 16}) || !writer.binary({result->object, 32}) ||
        !writer.binary({result->revision_hash, 32}) || !writer.unsignedInteger(result->generation) || !writer.unsignedInteger(result->revision) ||
        !writer.binary({result->directory, 16}) || !writer.binary({result->directory_epoch, 16}) || !writer.unsignedInteger(1)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace hostlink::management::publication_runtime
