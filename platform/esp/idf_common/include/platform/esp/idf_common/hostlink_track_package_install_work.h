#pragma once
#include "hostlink/route_storage_lease.h"
#include "platform/esp/idf_common/hostlink_prepare_scheduler_work.h"
#include "platform/esp/idf_common/hostlink_track_package_route_publication.h"
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
#include <new>
namespace hostlink::management::package_install_runtime
{
using PackageInstallPolicy = Status (*)(const char*, uint8_t*, size_t);
class PackageInstallWork final : public prepare_runtime::PrepareSchedulerWork
{
  public:
    PackageInstallWork(const track_sharing::PrepareRequest& request, const uint8_t* session, const uint8_t* actor, uint32_t media, uint64_t now, PackageInstallPolicy policy)
        : media_(media), generation_(request.generation), last_(now), policy_(policy), has_mapping_(request.package_upload.size == 16)
    {
        std::memcpy(session_, session, 16);
        std::memcpy(actor_, actor, 32);
        std::memcpy(ids_, request.kml_upload.data, 16);
        std::memcpy(ids_ + 16, request.manifest_upload.data, 16);
        if (has_mapping_) std::memcpy(ids_ + 32, request.package_upload.data, 16);
        std::memcpy(result_.operation, request.operation.data, 16);
        std::memcpy(result_.object, request.expected_object.data, 32);
        std::memcpy(result_.hash, request.expected_revision.data, 32);
    }
    ~PackageInstallWork() override { releaseStages(); }
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
        return request.install_request && request.package_request && matchesSession(session) && request.generation == generation_ && !request.expected_parent.size &&
               request.operation.data && request.operation.size == 16 && !std::memcmp(request.operation.data, result_.operation, 16) &&
               request.kml_upload.data && request.kml_upload.size == 16 && !std::memcmp(request.kml_upload.data, ids_, 16) &&
               request.manifest_upload.data && request.manifest_upload.size == 16 && !std::memcmp(request.manifest_upload.data, ids_ + 16, 16) &&
               (has_mapping_ ? request.package_upload.data && request.package_upload.size == 16 && !std::memcmp(request.package_upload.data, ids_ + 32, 16) : !request.package_upload.size) &&
               request.expected_object.data && request.expected_object.size == 32 && !std::memcmp(request.expected_object.data, result_.object, 32) &&
               request.expected_revision.data && request.expected_revision.size == 32 && !std::memcmp(request.expected_revision.data, result_.hash, 32);
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
        auto* target = reinterpret_cast<char*>(scratch + capacity - 80);
        if (!track_sharing::routePackageTarget({result_.hash, 32}, track_sharing::PackageRouteFile::Kml, target, 80)) return fail(Status::InvalidField);
        const auto allowed = policy_(target, scratch, capacity);
        if (allowed != Status::Ok) return allowed == Status::Busy ? allowed : fail(allowed);
        if (!input_)
        {
            input_ = createPackageInputOwner(media_, generation_, actor_, {ids_, 16}, {ids_ + 16, 16}, has_mapping_ ? geocaching::ByteView{ids_ + 32, 16} : geocaching::ByteView{}, {result_.object, 32}, {result_.hash, 32});
            if (!input_) return Status::Busy;
            const auto begun = input_->begin(scratch, capacity);
            if (begun != track_sharing::ManifestFileResult::Pending) return fail(map(begun));
        }
        if (!input_->ready())
        {
            const auto checked = input_->step(generation_, actor_, scratch, capacity);
            if (checked == track_sharing::ManifestFileResult::Pending) return Status::Busy;
            if (checked != track_sharing::ManifestFileResult::Ready) return fail(map(checked));
            result_.revision = input_->verified()->manifest.revision;
        }
        if (!cache_)
        {
            cache_ = createPublicPackageRetention(*input_, media_);
            if (!cache_) return Status::Busy;
        }
        if (!cache_->ready())
        {
            const auto cached = cache_->step(generation_, actor_, scratch, capacity);
            if (cached == Status::Busy) return cached;
            if (cached != Status::Ok) return fail(cached);
        }
        if (!publication_)
        {
            publication_ = createPackageRoutePublication(*input_, *cache_, media_);
            if (!publication_) return Status::Busy;
        }
        const auto published = publication_->step(generation_, actor_, scratch, capacity, [&](const char* path)
                                                  { return policy_(path, scratch, capacity); });
        if (published == Status::Busy) return published;
        if (published != Status::Ok) return fail(published);
        terminal_ = true;
        status_ = Status::Ok;
        releaseStages();
        return status_;
    }
    const prepare_runtime::TrackPreparedResult* result() const override { return ready() ? &result_ : nullptr; }
    geocaching::ByteView preparedSignature() const override { return {}; }
    bool ready() const override { return identity_ && terminal_ && status_ == Status::Ok && media_ == sd::sd_media_session() && sd::sd_card_ready(); }
    Status status() const override { return terminal_ ? status_ : Status::Busy; }
    bool packageInstallation() const override { return true; }

  private:
    static Status map(track_sharing::ManifestFileResult value) { return value == track_sharing::ManifestFileResult::StorageFailure ? Status::StorageUnavailable : value == track_sharing::ManifestFileResult::HashMismatch ? Status::HashMismatch
                                                                                                                                                                                                                           : Status::InvalidField; }
    Status fail(Status value)
    {
        terminal_ = true;
        status_ = value;
        releaseStages();
        return value;
    }
    void releaseStages()
    {
        destroyPackageRoutePublication(publication_);
        publication_ = nullptr;
        destroyPublicPackageRetention(cache_);
        cache_ = nullptr;
        destroyPackageInputOwner(input_);
        input_ = nullptr;
        if (lease_)
        {
            hostlink::route_storage_lease::release();
            lease_ = false;
        }
    }
    uint8_t session_[16]{}, actor_[32]{}, ids_[48]{};
    prepare_runtime::TrackPreparedResult result_;
    uint32_t media_, generation_;
    uint64_t last_;
    PackageInstallPolicy policy_;
    TrackPackageInputOwner* input_ = nullptr;
    TrackPublicPackageRetention* cache_ = nullptr;
    TrackPackageRoutePublication* publication_ = nullptr;
    Status status_ = Status::Busy;
    bool has_mapping_, identity_ = false, terminal_ = false, lease_ = true;
};
static_assert(sizeof(PackageInstallWork) <= 288, "Installed result metadata must stay small and transient");
inline PackageInstallWork* createPackageInstallWork(const track_sharing::PrepareRequest& request, const uint8_t* session, const uint8_t* actor, uint32_t media, uint64_t now, PackageInstallPolicy policy)
{
    if (!policy || !session || !actor || request.query_request || !request.install_request || !request.package_request || request.expected_parent.size || !request.generation ||
        !request.operation.data || request.operation.size != 16 || !request.kml_upload.data || request.kml_upload.size != 16 || !request.manifest_upload.data || request.manifest_upload.size != 16 ||
        (request.package_upload.size && (!request.package_upload.data || request.package_upload.size != 16)) || !request.expected_object.data || request.expected_object.size != 32 || !request.expected_revision.data || request.expected_revision.size != 32 ||
        media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    if (!hostlink::route_storage_lease::acquire()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(PackageInstallWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
    {
        hostlink::route_storage_lease::release();
        return nullptr;
    }
    return new (memory) PackageInstallWork(request, session, actor, media, now, policy);
}
} // namespace hostlink::management::package_install_runtime
