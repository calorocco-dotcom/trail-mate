#pragma once
#include "platform/esp/idf_common/hostlink_track_package_input_owner.h"
#include "track_sharing/installed_package_binding.h"
#include "track_sharing/package_cache_paths.h"
#include "track_sharing/signed_manifest_reader.h"
namespace hostlink::management::package_install_runtime
{
class CachedPackageResolver
{
  public:
    using Observer = bool (*)(void*, uint8_t, const track_sharing::AttachmentEntryView&);
    explicit CachedPackageResolver(const track_sharing::InstalledPackageBinding& binding) : binding_(binding) {}
    void observe(Observer callback, void* context)
    {
        observer_ = callback;
        context_ = context;
    }
    track_sharing::AttachmentResolveResult resolve(uint8_t ordinal, const track_sharing::AttachmentEntryView& entry, char* output, size_t capacity)
    {
        using namespace track_sharing;
        if (ordinal >= binding_.count) return AttachmentResolveResult::Invalid;
        if (!(binding_.availability & (uint64_t(1) << ordinal))) return AttachmentResolveResult::Missing;
        // Candidate notification only; consumers expose results only after the
        // entire owner is Ready, including original signature and actual KML.
        if (observer_ && !observer_(context_, ordinal, entry)) return AttachmentResolveResult::Invalid;
        return resolvePackageCachePath({binding_.revision, 32}, ObjectFile::AttachmentResource, entry.hash, output, capacity) ? AttachmentResolveResult::Ready : AttachmentResolveResult::Invalid;
    }

  private:
    const track_sharing::InstalledPackageBinding& binding_;
    Observer observer_ = nullptr;
    void* context_ = nullptr;
};
// Read-only fresh disk proof, independent of upload journals and creator's
// local identity generation. CRC record is a locator, never author proof.
class StoredPackageInputOwner
{
  public:
    StoredPackageInputOwner(uint32_t media, geocaching::ByteView object, geocaching::ByteView revision)
        : media_(media), file_(sd::SdRuntimeMemoryPolicy::PsramOnly), io_(file_, media), hash_(context_), asset_hash_(asset_context_),
          reader_(io_), kml_(io_, hash_, names_, sizeof(names_)), resolver_(binding_)
    {
        std::memcpy(object_, object.data, 32);
        std::memcpy(revision_, revision.data, 32);
    }
    ~StoredPackageInputOwner()
    {
        if (package_) package_->~Package();
    }
    bool hasBacking() const { return file_.has_backing_storage(); }
    bool observeAttachments(CachedPackageResolver::Observer callback, void* context)
    {
        if (state_ != track_sharing::ManifestFileResult::Idle) return false;
        resolver_.observe(callback, context);
        return true;
    }
    track_sharing::ManifestFileResult begin(uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (state_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!scratch || capacity < 448 || !mediaReady()) return state_ = ManifestFileResult::StorageFailure;
        if (!routePackageTarget({revision_, 32}, PackageRouteFile::Kml, target_, sizeof(target_)) ||
            !routePackageTarget({revision_, 32}, PackageRouteFile::Binding, reinterpret_cast<char*>(scratch + capacity - 80), 80)) return state_ = ManifestFileResult::Invalid;
        const auto marker = io_.open(reinterpret_cast<char*>(scratch + capacity - 80), sd::SdBorrowedRecordFileIo::Mode::Read);
        if (!marker.handle)
        {
            if (marker.status != sd::SdBorrowedRecordFileIo::OpenStatus::Missing) return state_ = ManifestFileResult::StorageFailure;
            const auto route = io_.open(target_, sd::SdBorrowedRecordFileIo::Mode::Read);
            if (route.handle)
            {
                io_.close(route.handle);
                return state_ = ManifestFileResult::Invalid;
            }
            if (route.status != sd::SdBorrowedRecordFileIo::OpenStatus::Missing) return state_ = ManifestFileResult::StorageFailure;
            missing_ = true;
            return state_ = ManifestFileResult::Ready;
        }
        uint64_t size = 0;
        const bool read = io_.size(marker.handle, size) && size == kInstalledPackageBindingBytes && io_.read(marker.handle, scratch, kInstalledPackageBindingBytes) == kInstalledPackageBindingBytes;
        const bool closed = io_.close(marker.handle);
        if (!read || !closed) return state_ = ManifestFileResult::StorageFailure;
        if (!decodeInstalledPackageBinding(scratch, kInstalledPackageBindingBytes, binding_) || std::memcmp(binding_.object, object_, 32) || std::memcmp(binding_.revision, revision_, 32)) return state_ = ManifestFileResult::Invalid;
        const auto route = io_.open(target_, sd::SdBorrowedRecordFileIo::Mode::Read);
        if (!route.handle)
        {
            if (route.status != sd::SdBorrowedRecordFileIo::OpenStatus::Missing) return state_ = ManifestFileResult::StorageFailure;
            missing_ = true;
            return state_ = ManifestFileResult::Ready;
        }
        if (!io_.close(route.handle)) return state_ = ManifestFileResult::StorageFailure;
        if (!resolvePackageCachePath({revision_, 32}, ObjectFile::SignedManifest, {}, signed_path_, sizeof(signed_path_))) return state_ = ManifestFileResult::Invalid;
        return state_ = reader_.begin(signed_path_, raw_, sizeof(raw_)) == FileIntegrityResult::Pending ? ManifestFileResult::Pending : ManifestFileResult::StorageFailure;
    }
    track_sharing::ManifestFileResult step(uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (!mediaReady())
        {
            cancel();
            return state_ = ManifestFileResult::StorageFailure;
        }
        if (state_ != ManifestFileResult::Pending) return state_;
        if (checking_kml_) return state_ = kml_.step(scratch, capacity);
        if (!authenticated_)
        {
            const auto read = reader_.step();
            if (read == FileIntegrityResult::Pending) return state_;
            if (read != FileIntegrityResult::Ready) return state_ = ManifestFileResult::StorageFailure;
            if (verifyManifest(reader_.bytes(), crypto_, hash_, workspace_, sizeof(workspace_), verified_) != ManifestVerification::Valid ||
                std::memcmp(verified_.object_id, object_, 32) || std::memcmp(verified_.revision_hash, revision_, 32) ||
                verified_.manifest.file_bytes != binding_.kml_bytes || std::memcmp(verified_.manifest.file_hash.data, binding_.kml_hash, 32)) return state_ = ManifestFileResult::Invalid;
            if (verified_.manifest.version == 1)
            {
                if (binding_.count) return state_ = ManifestFileResult::Invalid;
            }
            else
            {
                if (!getManifestExtensions(verified_.manifest, extensions_) || extensions_.assets.count != binding_.count || extensions_.assets.bytes != binding_.index_bytes ||
                    (binding_.count && std::memcmp(extensions_.assets.hash.data, binding_.index_hash, 32))) return state_ = ManifestFileResult::Invalid;
            }
            authenticated_ = true;
            if (binding_.count)
            {
                package_ = new (package_storage_) Package(io_, hash_, asset_hash_, resolver_, window_, sizeof(window_), fingerprints_, 64,
                                                          extensions_.assets, verified_.manifest.file_hash, verified_.manifest.file_bytes);
                if (!resolvePackageCachePath({revision_, 32}, ObjectFile::AttachmentIndex, {}, index_path_, sizeof(index_path_)) ||
                    package_->begin(index_path_) != AttachmentPackageResult::Pending) return state_ = ManifestFileResult::Invalid;
            }
        }
        if (package_ && !package_->ready())
        {
            const auto checked = package_->step();
            if (checked == AttachmentPackageResult::Pending) return state_;
            if (checked != AttachmentPackageResult::Ready) return state_ = checked == AttachmentPackageResult::HashMismatch ? ManifestFileResult::HashMismatch : checked == AttachmentPackageResult::StorageFailure ? ManifestFileResult::StorageFailure
                                                                                                                                                                                                                    : ManifestFileResult::Invalid;
        }
        const auto begun = verified_.manifest.version == 2 ? kml_.beginVerifiedPackage(target_, verified_, *this) : kml_.begin(target_, verified_);
        checking_kml_ = begun == ManifestFileResult::Pending;
        return state_ = begun;
    }
    bool matchesAttachments(const track_sharing::ManifestView& candidate) const
    {
        return authenticated_ && candidate.raw.data == verified_.manifest.raw.data && candidate.raw.size == verified_.manifest.raw.size &&
               (!binding_.count || (package_ && package_->ready()));
    }
    void cancel()
    {
        reader_.cancel();
        kml_.cancel();
        if (package_) package_->cancel();
        state_ = track_sharing::ManifestFileResult::Cancelled;
    }
    bool ready() const { return state_ == track_sharing::ManifestFileResult::Ready && !missing_ && authenticated_ && kml_.ready() && mediaReady(); }
    bool missing() const { return state_ == track_sharing::ManifestFileResult::Ready && missing_ && mediaReady(); }
    bool mediaReady() const { return media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }

  private:
    using Package = track_sharing::AttachmentPackageFileCheck<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256, CachedPackageResolver, 160>;
    uint32_t media_;
    uint8_t object_[32]{}, revision_[32]{};
    char target_[80]{}, signed_path_[160]{}, index_path_[160]{}, names_[2048]{};
    uint8_t raw_[4166]{}, workspace_[4200]{}, window_[4352]{};
    track_sharing::AttachmentFingerprint fingerprints_[64]{};
    track_sharing::InstalledPackageBinding binding_;
    track_sharing::VerifiedManifestView verified_;
    track_sharing::ManifestExtensionsView extensions_;
    mbedtls_sha256_context context_, asset_context_;
    sd::SdRuntimeFile file_;
    sd::SdBorrowedRecordFileIo io_;
    platform::esp::storage::SnapshotStreamSha256 hash_, asset_hash_;
    platform::esp::storage::TrackManifestCrypto crypto_;
    track_sharing::SignedManifestReader<sd::SdBorrowedRecordFileIo> reader_;
    track_sharing::ManifestFileCheck<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> kml_;
    CachedPackageResolver resolver_;
    alignas(Package) uint8_t package_storage_[sizeof(Package)];
    Package* package_ = nullptr;
    track_sharing::ManifestFileResult state_ = track_sharing::ManifestFileResult::Idle;
    bool authenticated_ = false, checking_kml_ = false, missing_ = false;
};
static_assert(sizeof(StoredPackageInputOwner) <= 24576, "Stored package query must remain bounded transient PSRAM");
inline StoredPackageInputOwner* createStoredPackageInputOwner(uint32_t media, geocaching::ByteView object, geocaching::ByteView revision)
{
    if (!object.data || object.size != 32 || !revision.data || revision.size != 32 || media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(StoredPackageInputOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* owner = new (memory) StoredPackageInputOwner(media, object, revision);
    if (owner->hasBacking()) return owner;
    owner->~StoredPackageInputOwner();
    heap_caps_free(memory);
    return nullptr;
}
inline void destroyStoredPackageInputOwner(StoredPackageInputOwner* owner)
{
    if (owner)
    {
        owner->cancel();
        owner->~StoredPackageInputOwner();
        heap_caps_free(owner);
    }
}
} // namespace hostlink::management::package_install_runtime
