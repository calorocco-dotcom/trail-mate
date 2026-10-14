#pragma once
#include "platform/esp/idf_common/hostlink_track_public_package_retention.h"
#include "track_sharing/package_route_publication.h"
namespace hostlink::management::package_install_runtime
{
inline Status admitPackageRouteStorage(uint32_t media, geocaching::ByteView revision, uint32_t bytes, uint8_t* scratch, size_t capacity)
{
    constexpr uint64_t limit = 32ULL * 1024 * 1024 + 65536;
    constexpr unsigned file_limit = 256;
    if (!scratch || capacity < 448 || !bytes || bytes > 8388608 || track_sharing::signingSpansOverlap(revision.data, revision.size, scratch, capacity)) return Status::InvalidField;
    if (!track_sharing::routePackageTarget(revision, track_sharing::PackageRouteFile::Kml, reinterpret_cast<char*>(scratch), 80) ||
        !track_sharing::routePackageTarget(revision, track_sharing::PackageRouteFile::Temporary, reinterpret_cast<char*>(scratch + 80), 80) ||
        !track_sharing::routePackageTarget(revision, track_sharing::PackageRouteFile::Binding, reinterpret_cast<char*>(scratch + 160), 80) ||
        !track_sharing::routePackageTarget(revision, track_sharing::PackageRouteFile::BindingTemporary, reinterpret_cast<char*>(scratch + 240), 80)) return Status::InvalidField;
    const char* final_name = std::strrchr(reinterpret_cast<char*>(scratch), '/') + 1;
    const char* temporary_name = std::strrchr(reinterpret_cast<char*>(scratch + 80), '/') + 1;
    const char* record_name = std::strrchr(reinterpret_cast<char*>(scratch + 160), '/') + 1;
    const char* record_temporary_name = std::strrchr(reinterpret_cast<char*>(scratch + 240), '/') + 1;
    sd::SdRuntimeDir directory(sd::SdRuntimeMemoryPolicy::PsramOnly);
    if (!directory.has_backing_storage()) return Status::Busy;
    const auto opened = directory.open_read_status("/routes", media);
    if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
    if (opened != sd::SdFileReadStatus::Ready) return Status::StorageUnavailable;
    uint64_t total = 0, partial = 0, record_partial = 0;
    unsigned files = 0;
    bool has_final = false, has_temporary = false;
    bool has_record = false, has_record_temporary = false;
    auto* name = reinterpret_cast<char*>(scratch + capacity - 128);
    for (unsigned scan = 0; scan <= file_limit; ++scan)
    {
        bool is_dir = false;
        uint64_t length = 0, next = 0;
        const auto row = directory.read_next_info(name, 128, &is_dir, &length, &next);
        if (row == sd::SdDirReadStatus::Busy) return Status::Busy;
        if (row == sd::SdDirReadStatus::End)
        {
            const uint64_t additional = (has_final ? 0 : bytes - partial) + (has_record ? 0 : track_sharing::kInstalledPackageBindingBytes - record_partial);
            const unsigned needed = unsigned(!has_final && !has_temporary) + unsigned(!has_record && !has_record_temporary);
            if (files > file_limit - needed || additional > limit - total) return Status::LimitExceeded;
            return media == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active() ? Status::Ok : Status::StorageUnavailable;
        }
        if (row != sd::SdDirReadStatus::Entry) return Status::StorageUnavailable;
        if (is_dir || ++files > file_limit || length > limit - total) return Status::LimitExceeded;
        total += length;
        if (!std::strcmp(name, final_name)) has_final = true;
        if (!std::strcmp(name, record_name)) has_record = true;
        if (!std::strcmp(name, record_temporary_name))
        {
            if (length > track_sharing::kInstalledPackageBindingBytes) return Status::Conflict;
            has_record_temporary = true;
            record_partial = length;
        }
        if (!std::strcmp(name, temporary_name))
        {
            if (length > bytes) return Status::Conflict;
            has_temporary = true;
            partial = length;
        }
    }
    return Status::LimitExceeded;
}
// Final visibility stage, allocated only after the public cache is Ready.
// Caller retains scheduler/storage/active-route exclusion through destruction.
class TrackPackageRoutePublication
{
  public:
    TrackPackageRoutePublication(TrackPackageInputOwner& input, TrackPublicPackageRetention& cache, uint32_t media)
        : input_(input), cache_(cache), media_(media), publish_(input.io(), input.hash(), binding_, source_)
    {
        const auto* verified = input.verified();
        if (!verified) return;
        std::memcpy(binding_.object, verified->object_id, 32);
        std::memcpy(binding_.revision, verified->revision_hash, 32);
        std::memcpy(binding_.kml_hash, verified->manifest.file_hash.data, 32);
        binding_.kml_bytes = verified->manifest.file_bytes;
        if (verified->manifest.version == 2 && track_sharing::getManifestExtensions(verified->manifest, extensions_) && extensions_.assets.count)
        {
            std::memcpy(binding_.index_hash, extensions_.assets.hash.data, 32);
            binding_.index_bytes = extensions_.assets.bytes;
            binding_.count = extensions_.assets.count;
            if (input.attachments()) binding_.availability = input.attachments()->availability();
        }
        initialized_ = track_sharing::resolvePackageCachePath({binding_.revision, 32}, track_sharing::ObjectFile::KmlFile, {}, source_, sizeof(source_)) &&
                       publish_.begin() == track_sharing::PackageRouteResult::Pending;
    }
    template <class Policy>
    Status step(uint32_t generation, const uint8_t* actor, uint8_t* scratch, size_t capacity, Policy&& policy)
    {
        if (!initialized_ || !scratch || capacity < 448) return Status::InvalidField;
        const auto current = input_.step(generation, actor, scratch, capacity);
        if (current != track_sharing::ManifestFileResult::Ready || !cache_.ready() || media_ != sd::sd_media_session())
        {
            publish_.cancel();
            return current == track_sharing::ManifestFileResult::StorageFailure || media_ != sd::sd_media_session() ? Status::StorageUnavailable : Status::StaleIdentity;
        }
        const auto allowed = policy(publish_.target());
        if (allowed != Status::Ok) return allowed;
        sd::SdRuntimeDir directory(sd::SdRuntimeMemoryPolicy::PsramOnly);
        if (!directory.has_backing_storage()) return Status::Busy;
        const auto opened = directory.open_read_status("/routes", media_);
        if (opened == sd::SdFileReadStatus::Busy) return Status::Busy;
        if (opened != sd::SdFileReadStatus::Ready && (opened != sd::SdFileReadStatus::Missing || !sd::sd_mkdir("/routes") || media_ != sd::sd_media_session())) return Status::StorageUnavailable;
        const auto admitted = admitPackageRouteStorage(media_, {binding_.revision, 32}, binding_.kml_bytes, scratch, capacity);
        if (admitted != Status::Ok) return admitted;
        const auto result = publish_.step(scratch, capacity, [&]
                                          { return input_.ready() && cache_.ready() && media_ == sd::sd_media_session() && policy(publish_.target()) == Status::Ok; });
        return result == track_sharing::PackageRouteResult::Pending ? Status::Busy : result == track_sharing::PackageRouteResult::Ready        ? Status::Ok
                                                                                 : result == track_sharing::PackageRouteResult::HashMismatch   ? Status::HashMismatch
                                                                                 : result == track_sharing::PackageRouteResult::StorageFailure ? Status::StorageUnavailable
                                                                                 : result == track_sharing::PackageRouteResult::Cancelled      ? Status::Expired
                                                                                                                                               : Status::Conflict;
    }
    bool ready() const { return publish_.ready() && input_.ready() && cache_.ready() && media_ == sd::sd_media_session(); }
    const char* target() const { return publish_.target(); }

  private:
    TrackPackageInputOwner& input_;
    TrackPublicPackageRetention& cache_;
    uint32_t media_;
    track_sharing::InstalledPackageBinding binding_;
    track_sharing::ManifestExtensionsView extensions_;
    char source_[160]{};
    track_sharing::PackageRoutePublication<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> publish_;
    bool initialized_ = false;
};
static_assert(sizeof(TrackPackageRoutePublication) <= 1280, "Package publication must not own payload/parser/hash workspaces");
inline TrackPackageRoutePublication* createPackageRoutePublication(TrackPackageInputOwner& input, TrackPublicPackageRetention& cache, uint32_t media)
{
    if (!input.ready() || !cache.ready() || media != sd::sd_media_session()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackPackageRoutePublication), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? new (memory) TrackPackageRoutePublication(input, cache, media) : nullptr;
}
inline void destroyPackageRoutePublication(TrackPackageRoutePublication* owner)
{
    if (owner)
    {
        owner->~TrackPackageRoutePublication();
        heap_caps_free(owner);
    }
}
} // namespace hostlink::management::package_install_runtime
