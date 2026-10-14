#pragma once
#include "hostlink/route_storage_lease.h"
#include "platform/esp/arduino_common/route_image_source_runtime.h"
#include "platform/esp/idf_common/hostlink_track_stored_package_input.h"
#include "track_sharing/installed_package_path.h"
#include <algorithm>
namespace platform::esp::route_images
{
namespace package = hostlink::management::package_install_runtime;
// One transient worker per existing image batch, no global slot. Candidate
// reference/resource mappings are unusable until the whole stored package is
// verified. The large input owner is released before copying/decoding images.
class RoutePackageImageWork
{
  public:
    explicit RoutePackageImageWork(uint32_t media)
        : media_(media), file_(sd::SdRuntimeMemoryPolicy::PsramOnly), io_(file_, media), hash_(context_) {}
    ~RoutePackageImageWork() { package::destroyStoredPackageInputOwner(owner_); }
    bool hasBacking() const { return lease_.acquired() && file_.has_backing_storage() && current(); }
    track_sharing::ManifestFileResult begin(std::string_view path)
    {
        using namespace track_sharing;
        if (state_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!hasBacking() || !installedPackageRevision(path, revision_) ||
            !routePackageTarget({revision_, 32}, PackageRouteFile::Binding, resource_path_, sizeof(resource_path_))) return state_ = ManifestFileResult::Invalid;
        const auto opened = io_.open(resource_path_, sd::SdBorrowedRecordFileIo::Mode::Read);
        if (!opened.handle) return state_ = ManifestFileResult::StorageFailure;
        uint64_t bytes = 0;
        const bool loaded = io_.size(opened.handle, bytes) && bytes == kInstalledPackageBindingBytes && io_.read(opened.handle, scratch_, bytes) == bytes;
        const bool closed = io_.close(opened.handle);
        if (!loaded || !closed || !decodeInstalledPackageBinding(scratch_, kInstalledPackageBindingBytes, binding_) || std::memcmp(binding_.revision, revision_, 32)) return state_ = ManifestFileResult::Invalid;
        std::snprintf(destination_root_, sizeof(destination_root_), "/routes/.trailmate/%.*s/images/", 66, path.data() + 8);
        owner_ = package::createStoredPackageInputOwner(media_, {binding_.object, 32}, {revision_, 32});
        if (!owner_) return state_ = ManifestFileResult::StorageFailure;
        if (!owner_->observeAttachments(&collect, this)) return state_ = ManifestFileResult::Invalid;
        state_ = owner_->begin(scratch_, sizeof(scratch_));
        return state_ == ManifestFileResult::Pending ? state_ : fail(state_ == ManifestFileResult::Ready ? ManifestFileResult::Invalid : state_);
    }
    track_sharing::ManifestFileResult step()
    {
        using namespace track_sharing;
        if (!current()) return fail(ManifestFileResult::StorageFailure);
        if (state_ != ManifestFileResult::Pending) return state_;
        state_ = owner_->step(scratch_, sizeof(scratch_));
        if (state_ == ManifestFileResult::Ready && !owner_->ready()) state_ = ManifestFileResult::Invalid;
        if (state_ != ManifestFileResult::Pending)
        {
            package::destroyStoredPackageInputOwner(owner_);
            owner_ = nullptr;
            if (state_ != ManifestFileResult::Ready) count_ = 0;
        }
        return state_;
    }
    bool ready() const { return state_ == track_sharing::ManifestFileResult::Ready && current(); }
    // The caller supplies an existing UI image path and an exact decoded KML
    // reference digest. Relative paths/URLs never turn into filesystem paths.
    // yield is called between small reads and readback hashes on the worker.
    bool materialize(const uint8_t reference[32], std::string_view destination, void (*yield)() = nullptr)
    {
        if (!ready() || !reference || destination.substr(0, std::strlen(destination_root_)) != destination_root_) return false;
        const Entry* matched = nullptr;
        for (unsigned i = 0; i < count_; ++i)
            if (!std::memcmp(entries_[i].reference, reference, 32))
            {
                matched = &entries_[i];
                break;
            }
        if (!matched || !resolvePackageCachePath({revision_, 32}, track_sharing::ObjectFile::AttachmentResource, {matched->hash, 32}, resource_path_, sizeof(resource_path_))) return false;
        void* memory = heap_caps_malloc(sizeof(RouteImageSourceWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return false;
        auto* output = ::new (memory) RouteImageSourceWork(sd::SdRuntimeMemoryPolicy::PsramOnly);
        bool ok = output->beginHashed(destination, reference);
        const auto input = ok ? io_.open(resource_path_, sd::SdBorrowedRecordFileIo::Mode::Read) : sd::SdBorrowedRecordFileIo::OpenResult{nullptr, sd::SdBorrowedRecordFileIo::OpenStatus::Unavailable};
        uint64_t bytes = 0;
        ok = ok && input.handle && io_.size(input.handle, bytes) && bytes == matched->bytes;
        uint32_t consumed = 0;
        while (ok && consumed < matched->bytes)
        {
            const auto wanted = std::min<size_t>(sizeof(scratch_), matched->bytes - consumed);
            const auto read = current() ? io_.read(input.handle, scratch_, wanted) : 0;
            ok = read == wanted && output->feed(scratch_, read);
            consumed += uint32_t(read);
            if (yield) yield();
        }
        if (input.handle && !io_.close(input.handle)) ok = false;
        ok = ok && current() && output->seal() && output->contentMatches(matched->bytes, matched->hash);
        auto checked = track_sharing::FileIntegrityResult::Pending;
        while (ok && checked == track_sharing::FileIntegrityResult::Pending)
        {
            if (!current())
            {
                ok = false;
                break;
            }
            checked = output->verifyStep();
            if (yield) yield();
        }
        ok = ok && checked == track_sharing::FileIntegrityResult::Ready && current() && output->commit();
        output->~RouteImageSourceWork();
        heap_caps_free(output);
        return ok;
    }

  private:
    struct Entry
    {
        uint8_t reference[32]{}, hash[32]{};
        uint32_t bytes = 0;
    };
    static bool collect(void* context, uint8_t, const track_sharing::AttachmentEntryView& entry)
    {
        auto& self = *static_cast<RoutePackageImageWork*>(context);
        if (!track_sharing::attachmentIdsEqual(entry.mime, "image/jpeg") && !track_sharing::attachmentIdsEqual(entry.mime, "image/png")) return true;
        if (self.count_ >= 64 || !entry.hash.data || entry.hash.size != 32 || entry.bytes > track_sharing::kImageSourceByteLimit) return false;
        auto& output = self.entries_[self.count_++];
        if (!self.hash_.start() || !self.hash_.update(reinterpret_cast<const uint8_t*>(entry.reference.data()), entry.reference.size()) || !self.hash_.finish(output.reference)) return false;
        std::memcpy(output.hash, entry.hash.data, 32);
        output.bytes = entry.bytes;
        return true;
    }
    bool current() const { return media_ && media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
    track_sharing::ManifestFileResult fail(track_sharing::ManifestFileResult result)
    {
        package::destroyStoredPackageInputOwner(owner_);
        owner_ = nullptr;
        count_ = 0;
        return state_ = result;
    }
    hostlink::route_storage_lease::Guard lease_;
    uint32_t media_;
    sd::SdRuntimeFile file_;
    sd::SdBorrowedRecordFileIo io_;
    mbedtls_sha256_context context_;
    platform::esp::storage::SnapshotStreamSha256 hash_;
    package::StoredPackageInputOwner* owner_ = nullptr;
    track_sharing::InstalledPackageBinding binding_;
    Entry entries_[64]{};
    uint8_t revision_[32]{}, scratch_[512]{};
    char resource_path_[160]{}, destination_root_[96]{};
    uint8_t count_ = 0;
    track_sharing::ManifestFileResult state_ = track_sharing::ManifestFileResult::Idle;
};
static_assert(sizeof(RoutePackageImageWork) <= 6144, "Package image mapping must stay bounded transient PSRAM");
inline RoutePackageImageWork* createRoutePackageImageWork()
{
    void* memory = heap_caps_malloc(sizeof(RoutePackageImageWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* work = new (memory) RoutePackageImageWork(sd::sd_media_session());
    if (work->hasBacking()) return work;
    work->~RoutePackageImageWork();
    heap_caps_free(work);
    return nullptr;
}
inline void destroyRoutePackageImageWork(RoutePackageImageWork* work)
{
    if (work)
    {
        work->~RoutePackageImageWork();
        heap_caps_free(work);
    }
}
} // namespace platform::esp::route_images
