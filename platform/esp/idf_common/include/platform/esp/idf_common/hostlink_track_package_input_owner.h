#pragma once
#include "platform/esp/common/storage/track_manifest_crypto.h"
#include "platform/esp/idf_common/hostlink_track_attachment_prepare_owner.h"
#include "track_sharing/attachment_upload_bindings.h"
#include "track_sharing/bound_sealed_upload.h"
#include "track_sharing/signed_package_file_check.h"
#include <esp_heap_caps.h>
#include <new>
namespace hostlink::management::package_install_runtime
{
namespace sd = platform::esp::arduino_common::storage;
// Public package input stage. Original signed bytes remain immutable. Local
// actor hash/generation authorizes uploads; remote author signature authorizes
// content. This does not publish a route, create an owned head or sign content.
class TrackPackageInputOwner
{
  public:
    TrackPackageInputOwner(uint32_t media, uint32_t generation, const uint8_t* actor,
                           geocaching::ByteView kml_id, geocaching::ByteView signed_id, geocaching::ByteView mapping_id,
                           geocaching::ByteView object, geocaching::ByteView revision)
        : media_(media), generation_(generation), has_mapping_(mapping_id.size == 16),
          file_(sd::SdRuntimeMemoryPolicy::PsramOnly), io_(file_, media_), hash_(context_),
          check_(io_, hash_, crypto_, signed_upload_, kml_upload_, generation_, actor_, names_, sizeof(names_), verified_, workspace_, sizeof(workspace_)),
          mapping_reader_(io_, hash_, mapping_upload_, generation_, actor_)
    {
        std::memcpy(actor_, actor, 32);
        std::memcpy(ids_, kml_id.data, 16);
        std::memcpy(ids_ + 16, signed_id.data, 16);
        if (has_mapping_) std::memcpy(ids_ + 32, mapping_id.data, 16);
        std::memcpy(object_, object.data, 32);
        std::memcpy(revision_, revision.data, 32);
    }
    ~TrackPackageInputOwner() { prepare_runtime::destroyAttachmentPrepareOwner(attachments_); }
    TrackPackageInputOwner(const TrackPackageInputOwner&) = delete;
    TrackPackageInputOwner& operator=(const TrackPackageInputOwner&) = delete;
    track_sharing::ManifestFileResult begin(uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (state_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!mediaReady()) return state_ = ManifestFileResult::StorageFailure;
        auto loaded = loadBoundSealedUpload(io_, {ids_, 16}, UploadKind::Kml, generation_, actor_, kml_upload_, verify_, kml_path_, scratch, capacity);
        if (loaded == JournalStoreResult::Ready) loaded = loadBoundSealedUpload(io_, {ids_ + 16, 16}, UploadKind::SignedManifest, generation_, actor_, signed_upload_, verify_, signed_path_, scratch, capacity);
        if (loaded != JournalStoreResult::Ready) return state_ = loaded == JournalStoreResult::StorageFailure ? ManifestFileResult::StorageFailure : ManifestFileResult::Invalid;
        return state_ = check_.begin(signed_path_, kml_path_, {object_, 32}, {revision_, 32}, raw_, sizeof(raw_));
    }
    track_sharing::ManifestFileResult step(uint32_t generation, const uint8_t* actor, uint8_t* scratch, size_t capacity)
    {
        using track_sharing::ManifestFileResult;
        if (!mediaReady())
        {
            cancel();
            return state_ = ManifestFileResult::StorageFailure;
        }
        if (!actor || generation != generation_ || std::memcmp(actor, actor_, 32))
        {
            cancel();
            return state_ = ManifestFileResult::Invalid;
        }
        return state_ == ManifestFileResult::Pending ? state_ = check_.step(*this, scratch, capacity) : state_;
    }
    track_sharing::ManifestFileResult checkAttachments(const track_sharing::ManifestView& candidate, uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (candidate.version == 1) return has_mapping_ ? ManifestFileResult::Invalid : ManifestFileResult::Ready;
        if (!getManifestExtensions(candidate, extensions_)) return ManifestFileResult::Invalid;
        if (!extensions_.assets.count) return has_mapping_ ? ManifestFileResult::Invalid : ManifestFileResult::Ready;
        if (!has_mapping_) return ManifestFileResult::Invalid;
        if (!mapping_started_)
        {
            const auto loaded = loadBoundSealedUpload(io_, {ids_ + 32, 16}, UploadKind::AttachmentBindings, generation_, actor_, mapping_upload_, verify_, mapping_path_, scratch, capacity);
            if (loaded != JournalStoreResult::Ready) return loaded == JournalStoreResult::StorageFailure ? ManifestFileResult::StorageFailure : ManifestFileResult::Invalid;
            if (mapping_reader_.begin(mapping_path_, mapping_raw_, sizeof(mapping_raw_)) != FileIntegrityResult::Pending) return ManifestFileResult::Invalid;
            mapping_started_ = true;
            return ManifestFileResult::Pending;
        }
        if (!attachments_)
        {
            const auto read = mapping_reader_.step(scratch, capacity);
            if (read == FileIntegrityResult::Pending) return ManifestFileResult::Pending;
            if (read != FileIntegrityResult::Ready) return read == FileIntegrityResult::HashMismatch ? ManifestFileResult::HashMismatch : ManifestFileResult::StorageFailure;
            if (!decodeAttachmentUploadBindings(mapping_reader_.bytes(), extensions_.assets.count, mapping_)) return ManifestFileResult::Invalid;
            attachments_ = prepare_runtime::createAttachmentPrepareOwner(media_, generation_, actor_, mapping_.index_id.data, mapping_.ids, extensions_.assets, candidate.file_hash, candidate.file_bytes);
            if (!attachments_) return ManifestFileResult::Pending;
            const auto begun = attachments_->begin();
            if (begun != AttachmentPackageResult::Pending) return begun == AttachmentPackageResult::StorageFailure ? ManifestFileResult::StorageFailure : ManifestFileResult::Invalid;
        }
        const auto checked = attachments_->step(generation_, actor_);
        return checked == AttachmentPackageResult::Pending ? ManifestFileResult::Pending : checked == AttachmentPackageResult::Ready        ? ManifestFileResult::Ready
                                                                                       : checked == AttachmentPackageResult::HashMismatch   ? ManifestFileResult::HashMismatch
                                                                                       : checked == AttachmentPackageResult::StorageFailure ? ManifestFileResult::StorageFailure
                                                                                                                                            : ManifestFileResult::Invalid;
    }
    bool matchesAttachments(const track_sharing::ManifestView& candidate) const
    {
        if (candidate.version == 1) return !has_mapping_ && !attachments_;
        if (!track_sharing::getManifestExtensions(candidate, extensions_)) return false;
        return extensions_.assets.count ? attachments_ && attachments_->matches(extensions_.assets, candidate.file_hash, candidate.file_bytes) : !has_mapping_ && !attachments_;
    }
    void cancel()
    {
        check_.cancel();
        mapping_reader_.cancel();
        if (attachments_) attachments_->cancel();
        state_ = track_sharing::ManifestFileResult::Cancelled;
    }
    bool ready() const { return state_ == track_sharing::ManifestFileResult::Ready && mediaReady() && check_.ready() &&
                                (verified_.manifest.version == 1 ? !has_mapping_ : matchesAttachments(verified_.manifest)); }
    bool hasBacking() const { return file_.has_backing_storage(); }
    bool mediaReady() const { return media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
    const track_sharing::VerifiedManifestView* verified() const { return ready() ? &verified_ : nullptr; }
    geocaching::ByteView signedBytes() const { return ready() ? check_.signedBytes() : geocaching::ByteView{}; }
    prepare_runtime::TrackAttachmentPrepareOwner* attachments() const { return ready() ? attachments_ : nullptr; }
    const char* kmlPath() const { return ready() ? kml_path_ : nullptr; }
    const char* signedPath() const { return ready() ? signed_path_ : nullptr; }
    const track_sharing::UploadJournal& signedUpload() const { return signed_upload_; }
    sd::SdBorrowedRecordFileIo& io() { return io_; }
    platform::esp::storage::SnapshotStreamSha256& hash() { return hash_; }

  private:
    uint32_t media_, generation_;
    bool has_mapping_;
    uint8_t actor_[32]{}, ids_[48]{}, object_[32]{}, revision_[32]{};
    char kml_path_[64]{}, signed_path_[64]{}, mapping_path_[64]{}, names_[2048]{};
    uint8_t raw_[4166]{}, workspace_[4200]{}, mapping_raw_[1100]{};
    track_sharing::UploadJournal kml_upload_, signed_upload_, mapping_upload_, verify_;
    track_sharing::VerifiedManifestView verified_;
    mutable track_sharing::ManifestExtensionsView extensions_;
    track_sharing::AttachmentUploadBindingsView mapping_;
    mbedtls_sha256_context context_;
    sd::SdRuntimeFile file_;
    sd::SdBorrowedRecordFileIo io_;
    platform::esp::storage::SnapshotStreamSha256 hash_;
    platform::esp::storage::TrackManifestCrypto crypto_;
    track_sharing::SignedPackageFileCheck<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256, decltype(crypto_)> check_;
    track_sharing::UploadedManifestReader<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256, 1100> mapping_reader_;
    prepare_runtime::TrackAttachmentPrepareOwner* attachments_ = nullptr;
    track_sharing::ManifestFileResult state_ = track_sharing::ManifestFileResult::Idle;
    bool mapping_started_ = false;
};
static_assert(sizeof(TrackPackageInputOwner) <= 18432, "Shared package input must remain bounded transient PSRAM");
inline TrackPackageInputOwner* createPackageInputOwner(uint32_t media, uint32_t generation, const uint8_t* actor,
                                                       geocaching::ByteView kml_id, geocaching::ByteView signed_id, geocaching::ByteView mapping_id,
                                                       geocaching::ByteView object, geocaching::ByteView revision)
{
    if (!actor || !generation || !kml_id.data || kml_id.size != 16 || !signed_id.data || signed_id.size != 16 ||
        (mapping_id.size && (!mapping_id.data || mapping_id.size != 16)) || !object.data || object.size != 32 || !revision.data || revision.size != 32 ||
        media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    for (const auto id : {kml_id, signed_id, object, revision})
    {
        uint8_t bits = 0;
        for (size_t i = 0; i < id.size; ++i) bits |= id.data[i];
        if (!bits) return nullptr;
    }
    if (mapping_id.size)
    {
        uint8_t bits = 0;
        for (size_t i = 0; i < 16; ++i) bits |= mapping_id.data[i];
        if (!bits) return nullptr;
    }
    void* memory = heap_caps_malloc(sizeof(TrackPackageInputOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* owner = new (memory) TrackPackageInputOwner(media, generation, actor, kml_id, signed_id, mapping_id, object, revision);
    if (owner->hasBacking()) return owner;
    owner->~TrackPackageInputOwner();
    heap_caps_free(memory);
    return nullptr;
}
inline void destroyPackageInputOwner(TrackPackageInputOwner* owner)
{
    if (owner)
    {
        owner->cancel();
        owner->~TrackPackageInputOwner();
        heap_caps_free(owner);
    }
}
} // namespace hostlink::management::package_install_runtime
