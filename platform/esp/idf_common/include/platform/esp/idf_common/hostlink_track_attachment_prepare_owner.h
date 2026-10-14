#pragma once
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/sealed_attachment_uploads.h"
#include <esp_heap_caps.h>
#include <new>

namespace hostlink::management::prepare_runtime
{
namespace sd = platform::esp::arduino_common::storage;
// Transient PSRAM only. The scheduler retains the immutable upload lease and
// authenticates identity before every step. No static slot/table or task stack
// buffers. This proof is a prerequisite of v2 preparation, not its completion.
class TrackAttachmentPrepareOwner
{
  public:
    TrackAttachmentPrepareOwner(uint32_t media, uint32_t generation, const uint8_t* author_hash,
                                const uint8_t* index_id, geocaching::ByteView ids, const track_sharing::AttachmentIndexBinding& binding,
                                geocaching::ByteView kml_hash, uint32_t kml_bytes)
        : media_(media), generation_(generation), count_(binding.count), index_bytes_(binding.bytes), kml_bytes_(kml_bytes),
          file_(sd::SdRuntimeMemoryPolicy::PsramOnly), io_(file_, media_), index_hash_(index_context_), resource_hash_(resource_context_),
          resolver_(io_, {ids_, ids.size}, generation_, author_),
          check_(io_, index_hash_, resource_hash_, resolver_, window_, sizeof(window_), fingerprints_, 64,
                 {{index_digest_, 32}, index_bytes_, count_}, {kml_digest_, 32}, kml_bytes)
    {
        std::memcpy(author_, author_hash, 32);
        std::memcpy(index_id_, index_id, 16);
        std::memcpy(ids_, ids.data, ids.size);
        std::memcpy(index_digest_, binding.hash.data, 32);
        std::memcpy(kml_digest_, kml_hash.data, 32);
    }
    TrackAttachmentPrepareOwner(const TrackAttachmentPrepareOwner&) = delete;
    TrackAttachmentPrepareOwner& operator=(const TrackAttachmentPrepareOwner&) = delete;
    track_sharing::AttachmentPackageResult begin()
    {
        using R = track_sharing::AttachmentPackageResult;
        if (started_) return R::Busy;
        started_ = true;
        if (!mediaReady()) return state_ = R::StorageFailure;
        const auto loaded = resolver_.index(index_id_, {{index_digest_, 32}, index_bytes_, count_}, path_, sizeof(path_));
        if (loaded != track_sharing::AttachmentResolveResult::Ready)
            return state_ = loaded == track_sharing::AttachmentResolveResult::StorageFailure ? R::StorageFailure : R::Invalid;
        return state_ = check_.begin(path_);
    }
    track_sharing::AttachmentPackageResult step(uint32_t generation, const uint8_t* author_hash)
    {
        using R = track_sharing::AttachmentPackageResult;
        if (!mediaReady())
        {
            check_.cancel();
            return state_ = R::StorageFailure;
        }
        if (generation != generation_ || !author_hash || std::memcmp(author_hash, author_, 32))
        {
            check_.cancel();
            return state_ = R::Invalid;
        }
        if (state_ != R::Pending) return state_;
        return state_ = check_.step();
    }
    void cancel()
    {
        check_.cancel();
        state_ = track_sharing::AttachmentPackageResult::Cancelled;
    }
    bool mediaReady() const { return media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
    bool ready() const { return state_ == track_sharing::AttachmentPackageResult::Ready && mediaReady() && check_.ready(); }
    bool matches(const track_sharing::AttachmentIndexBinding& binding, geocaching::ByteView kml_hash, uint32_t kml_bytes) const
    {
        return ready() && kml_bytes == kml_bytes_ && kml_hash.data && kml_hash.size == 32 && !std::memcmp(kml_hash.data, kml_digest_, 32) && check_.matchesIndex(path_, binding);
    }
    uint64_t availability() const { return ready() ? check_.availability() : 0; }
    bool allAvailable() const { return ready() && check_.allAvailable(); }
    bool hasBacking() const { return file_.has_backing_storage(); }
    const char* indexPath() const { return ready() ? path_ : nullptr; }
    track_sharing::AttachmentIndexBinding indexBinding() const { return {{index_digest_, 32}, index_bytes_, count_}; }
    geocaching::ByteView kmlHash() const { return {kml_digest_, 32}; }
    uint32_t kmlBytes() const { return kml_bytes_; }
    geocaching::ByteView uploadIds() const { return {ids_, size_t(count_) * 16}; }
    sd::SdBorrowedRecordFileIo& io() { return io_; }
    platform::esp::storage::SnapshotStreamSha256& indexHash() { return index_hash_; }
    platform::esp::storage::SnapshotStreamSha256& resourceHash() { return resource_hash_; }
    track_sharing::SealedAttachmentUploads<sd::SdBorrowedRecordFileIo>& resolver() { return resolver_; }

  private:
    uint32_t media_, generation_;
    uint8_t count_;
    uint32_t index_bytes_, kml_bytes_;
    uint8_t author_[32]{}, index_id_[16]{}, ids_[1024]{}, index_digest_[32]{}, kml_digest_[32]{};
    char path_[64]{};
    uint8_t window_[4352]{};
    track_sharing::AttachmentFingerprint fingerprints_[64]{};
    mbedtls_sha256_context index_context_, resource_context_;
    sd::SdRuntimeFile file_;
    sd::SdBorrowedRecordFileIo io_;
    platform::esp::storage::SnapshotStreamSha256 index_hash_, resource_hash_;
    track_sharing::SealedAttachmentUploads<sd::SdBorrowedRecordFileIo> resolver_;
    track_sharing::AttachmentPackageFileCheck<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256, decltype(resolver_)> check_;
    track_sharing::AttachmentPackageResult state_ = track_sharing::AttachmentPackageResult::Idle;
    bool started_ = false;
};
static_assert(sizeof(TrackAttachmentPrepareOwner) <= 9216, "Attachment preparation exceeds transient PSRAM budget");
inline TrackAttachmentPrepareOwner* createAttachmentPrepareOwner(uint32_t media, uint32_t generation, const uint8_t* author_hash,
                                                                 const uint8_t* index_id, geocaching::ByteView ids,
                                                                 const track_sharing::AttachmentIndexBinding& binding, geocaching::ByteView kml_hash, uint32_t kml_bytes)
{
    if (!generation || !author_hash || !index_id || !ids.data || !binding.count || binding.count > 64 || ids.size != size_t(binding.count) * 16 ||
        !binding.hash.data || binding.hash.size != 32 || !binding.bytes || binding.bytes > track_sharing::kMaxAttachmentIndexBytes ||
        !kml_hash.data || kml_hash.size != 32 || !kml_bytes || kml_bytes > 8388608 || media != sd::sd_media_session() ||
        !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    uint8_t bits = 0;
    for (size_t i = 0; i < 16; ++i) bits |= index_id[i];
    if (!bits) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackAttachmentPrepareOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* owner = new (memory) TrackAttachmentPrepareOwner(media, generation, author_hash, index_id, ids, binding, kml_hash, kml_bytes);
    if (owner->hasBacking()) return owner;
    owner->~TrackAttachmentPrepareOwner();
    heap_caps_free(memory);
    return nullptr;
}
inline void destroyAttachmentPrepareOwner(TrackAttachmentPrepareOwner* owner)
{
    if (!owner) return;
    owner->cancel();
    owner->~TrackAttachmentPrepareOwner();
    heap_caps_free(owner);
}
} // namespace hostlink::management::prepare_runtime
