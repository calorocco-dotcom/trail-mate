#pragma once
#include "hostlink/route_install_authorization.h"
#include "hostlink/route_install_job.h"
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/upload_journal_store.h"
#include <esp_heap_caps.h>
#include <new>
namespace hostlink::management::route_install_runtime
{
namespace sd = platform::esp::arduino_common::storage;
class RouteInstallOwner
{
  public:
    RouteInstallOwner(const RouteInstallView& request, const uint8_t* session, const uint8_t* author, uint32_t media, uint64_t now)
        : request_{{bindings_, 16}, {bindings_ + 16, 32}, request.bytes, request.generation}, media_(media), deadline_(now + 120000),
          file_(sd::SdRuntimeMemoryPolicy::PsramOnly), io_(file_, media), hash_(context_),
          job_(io_, hash_, names_, sizeof(names_), request_, source_, target_)
    {
        std::memcpy(bindings_, request.transfer.data, 16);
        std::memcpy(bindings_ + 16, request.hash.data, 32);
        std::memcpy(session_, session, 16);
        std::memcpy(author_, author, 32);
    }
    RouteInstallOwner(const RouteInstallOwner&) = delete;
    RouteInstallOwner& operator=(const RouteInstallOwner&) = delete;
    template <class Guard>
    Status start(bool install, uint8_t* scratch, size_t capacity, Guard&& live)
    {
        if (job_.phase() != RouteInstallPhase::Idle) return Status::Busy;
        query_only_ = !install;
        if (!scratch || capacity < 256 || !mediaReady()) return Status::StorageUnavailable;
        if (!track_sharing::resolveUploadPath(bindings_, 16, track_sharing::UploadFile::Candidate, source_, sizeof(source_)) ||
            !track_sharing::resolveUploadPath(bindings_, 16, track_sharing::UploadFile::JournalA, a_, sizeof(a_)) ||
            !track_sharing::resolveUploadPath(bindings_, 16, track_sharing::UploadFile::JournalB, b_, sizeof(b_))) return Status::InvalidField;
        // Local route confirmation proves public target bytes, not a new write.
        // Its upload journal may legitimately have been retired after install.
        if (query_only_) return job_.begin(false, [&]
                                           {const auto current=live();return current==Status::Ok&&!mediaReady()?Status::StorageUnavailable:current; });
        track_sharing::UploadJournalStore<decltype(io_)> store(io_, a_, b_);
        const auto loaded = store.load(journal_, verify_, scratch, capacity);
        if (loaded != track_sharing::JournalStoreResult::Ready) return loaded == track_sharing::JournalStoreResult::StorageFailure ? Status::StorageUnavailable : loaded == track_sharing::JournalStoreResult::Missing ? Status::Expired
                                                                                                                                                                                                                       : Status::InvalidField;
        const auto guard = [&]
        {const auto current=live();return current==Status::Ok?authorizeRouteInstallUpload(request_,journal_,track_sharing::UploadKind::Kml,request_.generation,{author_,32},true,mediaReady()):current; };
        return job_.begin(install, guard);
    }
    template <class Guard>
    Status advance(uint8_t* scratch, size_t capacity, Guard&& live)
    {
        if (!scratch || capacity < 256) return Status::InvalidField;
        const auto guard = [&]
        {
            const auto current = live();
            if (current != Status::Ok) return current;
            if (query_only_) return mediaReady() ? Status::Ok : Status::StorageUnavailable;
            const auto authorized = authorizeRouteInstallUpload(request_, journal_, track_sharing::UploadKind::Kml, request_.generation, {author_, 32}, true, mediaReady());
            if (authorized != Status::Ok) return authorized;
            track_sharing::UploadJournalStore<decltype(io_)> store(io_, a_, b_);
            const auto valid = store.validateCurrent(journal_, verify_, scratch, capacity);
            return valid == track_sharing::JournalStoreResult::Ready ? Status::Ok : valid == track_sharing::JournalStoreResult::StorageFailure ? Status::StorageUnavailable
                                                                                                                                               : Status::Conflict;
        };
        return job_.step(scratch, capacity, guard);
    }
    bool identityMatches(uint32_t generation, geocaching::ByteView hash) const
    {
        return generation == request_.generation && hash.data && hash.size == 32 && !std::memcmp(hash.data, author_, 32);
    }
    bool matches(const uint8_t* session, const RouteInstallView& request) const
    {
        return session && request.transfer.data && request.transfer.size == 16 && request.hash.data && request.hash.size == 32 &&
               !std::memcmp(session, session_, 16) && !std::memcmp(request.transfer.data, bindings_, 16) && !std::memcmp(request.hash.data, bindings_ + 16, 32) &&
               request.bytes == request_.bytes && request.generation == request_.generation;
    }
    bool matchesSession(const uint8_t* session) const { return session && !std::memcmp(session, session_, 16); }
    bool alive(uint64_t now, bool connected) const { return connected && now < deadline_ && mediaReady(); }
    void renew(uint64_t now) { deadline_ = now + 120000; }
    bool hasBacking() const { return file_.has_backing_storage(); }
    bool queryOnly() const { return query_only_; }
    uint32_t mediaSession() const { return media_; }
    bool resultDelivered() const { return result_delivered_; }
    void markResultDelivered()
    {
        if (job_.status() == Status::Ok) result_delivered_ = true;
    }
    Status status() const { return job_.status(); }
    uint8_t wireState() const { return job_.wireState(); }
    void cancel() { job_.cancel(); }
    const RouteInstallView& binding() const { return request_; }
    const char* target() const { return target_; }

  private:
    bool mediaReady() const { return media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
    uint8_t bindings_[48]{}, session_[16]{}, author_[32]{};
    RouteInstallView request_;
    uint32_t media_;
    uint64_t deadline_;
    bool query_only_ = true;
    bool result_delivered_ = false;
    track_sharing::UploadJournal journal_{}, verify_{};
    char source_[64]{}, target_[77]{}, a_[64]{}, b_[64]{}, names_[2048]{};
    mbedtls_sha256_context context_;
    sd::SdRuntimeFile file_;
    sd::SdBorrowedRecordFileIo io_;
    platform::esp::storage::SnapshotStreamSha256 hash_;
    RouteInstallJob<decltype(io_), decltype(hash_)> job_;
};
static_assert(sizeof(RouteInstallOwner) <= 12288, "Route owner exceeds transient PSRAM admission");
inline RouteInstallOwner* createRouteInstallOwner(const RouteInstallView& request, const uint8_t* session, const uint8_t* author, uint32_t media, uint64_t now)
{
    if (!session || !author || !request.transfer.data || request.transfer.size != 16 || !request.hash.data || request.hash.size != 32 ||
        !request.bytes || request.bytes > 8388608 || !request.generation || request.generation >= UINT32_MAX ||
        media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(RouteInstallOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* owner = new (memory) RouteInstallOwner(request, session, author, media, now);
    if (!owner->hasBacking())
    {
        owner->~RouteInstallOwner();
        heap_caps_free(memory);
        return nullptr;
    }
    return owner;
}
inline void destroyRouteInstallOwner(RouteInstallOwner* owner)
{
    if (!owner) return;
    owner->cancel();
    owner->~RouteInstallOwner();
    heap_caps_free(owner);
}
} // namespace hostlink::management::route_install_runtime
