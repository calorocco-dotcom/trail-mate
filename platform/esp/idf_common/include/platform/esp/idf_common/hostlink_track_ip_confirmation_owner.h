#pragma once
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "platform/esp/common/storage/track_manifest_crypto.h"
#include "platform/esp/common/storage/track_receipt_crypto.h"
#include "platform/esp/idf_common/hostlink_track_history_admission.h"
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
#include "track_sharing/ip_confirmation_work.h"
#include <esp_heap_caps.h>
#include <new>
namespace hostlink::management::ip_confirmation_runtime
{
namespace sd = platform::esp::arduino_common::storage;
// One transient PSRAM owner. Caller serializes storage against other HostLink
// work, owns session/media leases and destroys this after polling/release.
class TrackIpConfirmationOwner
{
  public:
    TrackIpConfirmationOwner(const track_sharing::IpConfirmationRequest& request, uint32_t media,
                             const uint8_t* author, const uint8_t* author_hash)
        : media_(media), file_(sd::SdRuntimeMemoryPolicy::PsramOnly), io_(file_, media_), hash_(context_),
          work_(io_, hash_, manifest_crypto_, receipt_crypto_, request, {author, 64}, {author_hash, 32},
                signed_storage_, sizeof(signed_storage_), receipt_storage_, sizeof(receipt_storage_), domain_storage_, sizeof(domain_storage_)) {}
    TrackIpConfirmationOwner(const TrackIpConfirmationOwner&) = delete;
    TrackIpConfirmationOwner& operator=(const TrackIpConfirmationOwner&) = delete;
    bool backingReady() const { return file_.has_backing_storage(); }
    Status advance(chat::IMeshAdapter& adapter, uint8_t* scratch, size_t capacity)
    {
        if (!scratch || capacity < 448) return Status::InvalidField;
        if (media_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active())
        {
            work_.cancel();
            return Status::StorageUnavailable;
        }
        uint8_t author_hash[32];
        uint32_t generation = 0;
        auto* author = scratch + capacity - 64;
        const auto identity = upload_runtime::readUploadAuthor(adapter, author, 64, author_hash, generation);
        if (identity != Status::Ok)
        {
            if (identity != Status::Busy) work_.cancel();
            return identity;
        }
        if (const auto* evidence = work_.importEvidence())
        {
            const auto admitted = admitImportedObjectDirectory(media_, *evidence, scratch, capacity);
            if (admitted != Status::Ok)
            {
                if (admitted != Status::Busy) work_.cancel();
                return admitted;
            }
            // Admission borrowed scratch. Restore current author before step.
            const auto refreshed = upload_runtime::readUploadAuthor(adapter, author, 64, author_hash, generation);
            if (refreshed != Status::Ok)
            {
                if (refreshed != Status::Busy) work_.cancel();
                return refreshed;
            }
        }
        switch (work_.step(generation, {author, 64}, {author_hash, 32}, true, scratch, capacity))
        {
        case track_sharing::IpConfirmationResult::Pending:
            return Status::Busy;
        case track_sharing::IpConfirmationResult::Ready:
            return Status::Ok;
        case track_sharing::IpConfirmationResult::StorageFailure:
            return Status::StorageUnavailable;
        case track_sharing::IpConfirmationResult::StaleIdentity:
            return Status::StaleIdentity;
        case track_sharing::IpConfirmationResult::HashMismatch:
            return Status::HashMismatch;
        case track_sharing::IpConfirmationResult::Cancelled:
            return Status::Expired;
        case track_sharing::IpConfirmationResult::Conflict:
            return Status::Conflict;
        default:
            return Status::InvalidField;
        }
    }
    const track_sharing::ObjectHead* result() const { return work_.result(); }

  private:
    uint32_t media_;
    sd::SdRuntimeFile file_;
    sd::SdBorrowedRecordFileIo io_;
    mbedtls_sha256_context context_;
    platform::esp::storage::SnapshotStreamSha256 hash_;
    platform::esp::storage::TrackManifestCrypto manifest_crypto_;
    platform::esp::storage::TrackReceiptCrypto receipt_crypto_;
    uint8_t signed_storage_[4166]{}, receipt_storage_[1100]{}, domain_storage_[4166]{};
    track_sharing::IpConfirmationWork<sd::SdBorrowedRecordFileIo, decltype(hash_), decltype(manifest_crypto_), decltype(receipt_crypto_)> work_;
};
static_assert(sizeof(TrackIpConfirmationOwner) <= 16384, "Confirmation must fit one bounded transient PSRAM owner");
inline TrackIpConfirmationOwner* createOwner(const track_sharing::IpConfirmationRequest& request, uint32_t media,
                                             const uint8_t* author, const uint8_t* author_hash)
{
    if (!author || !author_hash) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackIpConfirmationOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* owner = new (memory) TrackIpConfirmationOwner(request, media, author, author_hash);
    if (!owner->backingReady())
    {
        owner->~TrackIpConfirmationOwner();
        heap_caps_free(owner);
        return nullptr;
    }
    return owner;
}
inline void destroyOwner(TrackIpConfirmationOwner* owner)
{
    if (!owner) return;
    owner->~TrackIpConfirmationOwner();
    heap_caps_free(owner);
}
} // namespace hostlink::management::ip_confirmation_runtime
