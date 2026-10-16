#pragma once
#include "hostlink/route_storage_lease.h"
#include "platform/esp/idf_common/hostlink_prepare_scheduler_work.h"
#include "platform/esp/idf_common/hostlink_track_stored_package_input.h"
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
namespace hostlink::management::package_install_runtime
{
class PackageQueryWork final : public prepare_runtime::PrepareSchedulerWork
{
  public:
    PackageQueryWork(const track_sharing::PrepareRequest& request, const uint8_t* session, const uint8_t* actor, uint32_t media, uint64_t now)
        : media_(media), generation_(request.generation), last_(now)
    {
        std::memcpy(session_, session, 16);
        std::memcpy(actor_, actor, 32);
        std::memcpy(result_.operation, request.operation.data, 16);
        std::memcpy(result_.object, request.expected_object.data, 32);
        std::memcpy(result_.hash, request.expected_revision.data, 32);
    }
    ~PackageQueryWork() override { release(); }
    bool alive(uint32_t media, uint64_t now, bool connected) const override { return connected && media == media_ && now >= last_ && now - last_ < 30000; }
    bool matchesSession(const uint8_t* session) const override { return session && !std::memcmp(session, session_, 16); }
    bool holdsStages() const override { return lease_; }
    void suspendIdentity() override { identity_ = false; }
    geocaching::ByteView operationId() const override { return {result_.operation, 16}; }
    bool renewMatched(uint64_t now) override
    {
        if (!identity_ || !alive(sd::sd_media_session(), now, true)) return false;
        last_ = now;
        return true;
    }
    bool matches(const uint8_t* session, const track_sharing::PrepareRequest& request) const override
    {
        return request.query_request && matchesSession(session) && request.generation == generation_ && request.operation.data && request.operation.size == 16 && !std::memcmp(request.operation.data, result_.operation, 16) &&
               request.expected_object.data && request.expected_object.size == 32 && !std::memcmp(request.expected_object.data, result_.object, 32) && request.expected_revision.data && request.expected_revision.size == 32 && !std::memcmp(request.expected_revision.data, result_.hash, 32);
    }
    Status advance(chat::IMeshAdapter& adapter, uint8_t* scratch, size_t capacity) override
    {
        identity_ = false;
        if (!scratch || capacity < 448) return fail(Status::Internal);
        if (media_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return fail(Status::StorageUnavailable);
        uint8_t actor[32];
        uint32_t generation = 0;
        const auto current = upload_runtime::readUploadAuthor(adapter, scratch + capacity - 64, 64, actor, generation);
        if (current == Status::Busy) return current;
        if (current != Status::Ok) return fail(current);
        if (generation != generation_ || std::memcmp(actor, actor_, 32)) return fail(Status::StaleIdentity);
        identity_ = true;
        if (terminal_) return status_;
        track_sharing::ManifestFileResult checked;
        if (!input_)
        {
            input_ = createStoredPackageInputOwner(media_, {result_.object, 32}, {result_.hash, 32});
            if (!input_) return Status::Busy;
            checked = input_->begin(scratch, capacity);
        }
        else checked = input_->step(scratch, capacity);
        if (checked == track_sharing::ManifestFileResult::Pending) return Status::Busy;
        if (checked != track_sharing::ManifestFileResult::Ready) return fail(checked == track_sharing::ManifestFileResult::StorageFailure ? Status::StorageUnavailable : checked == track_sharing::ManifestFileResult::HashMismatch ? Status::HashMismatch
                                                                                                                                                                                                                                    : Status::InvalidField);
        if (!input_->ready() && !input_->missing()) return fail(Status::InvalidField);
        missing_ = input_->missing();
        terminal_ = true;
        status_ = Status::Ok;
        release();
        return status_;
    }
    const prepare_runtime::TrackPreparedResult* result() const override { return ready() ? &result_ : nullptr; }
    geocaching::ByteView preparedSignature() const override { return {}; }
    bool ready() const override { return identity_ && terminal_ && status_ == Status::Ok && media_ == sd::sd_media_session() && sd::sd_card_ready(); }
    Status status() const override { return terminal_ ? status_ : Status::Busy; }
    bool packageInstallation() const override { return true; }
    uint8_t installationState() const override { return ready() ? (missing_ ? 2 : 1) : 0; }

  private:
    Status fail(Status result)
    {
        terminal_ = true;
        status_ = result;
        release();
        return result;
    }
    void release()
    {
        destroyStoredPackageInputOwner(input_);
        input_ = nullptr;
        if (lease_)
        {
            hostlink::route_storage_lease::release();
            lease_ = false;
        }
    }
    uint8_t session_[16]{}, actor_[32]{};
    prepare_runtime::TrackPreparedResult result_;
    uint32_t media_, generation_;
    uint64_t last_;
    StoredPackageInputOwner* input_ = nullptr;
    Status status_ = Status::Busy;
    bool identity_ = false, terminal_ = false, missing_ = false, lease_ = true;
};
static_assert(sizeof(PackageQueryWork) <= 224, "Fresh query result must not retain parser buffers");
inline PackageQueryWork* createPackageQueryWork(const track_sharing::PrepareRequest& request, const uint8_t* session, const uint8_t* actor, uint32_t media, uint64_t now)
{
    if (!session || !actor || !request.query_request || !request.generation || !request.operation.data || request.operation.size != 16 || !request.expected_object.data || request.expected_object.size != 32 || !request.expected_revision.data || request.expected_revision.size != 32 || media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    if (!hostlink::route_storage_lease::acquire()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(PackageQueryWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
    {
        hostlink::route_storage_lease::release();
        return nullptr;
    }
    return new (memory) PackageQueryWork(request, session, actor, media, now);
}
} // namespace hostlink::management::package_install_runtime
