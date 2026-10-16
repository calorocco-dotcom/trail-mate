#pragma once
#include "track_sharing/cancel_publication.h"
#include "track_sharing/publication_reply_receiver.h"
#include "track_sharing/publication_send_step.h"
#include "track_sharing/request_retry_window.h"
#include "track_sharing/upload_send_step.h"

namespace track_sharing
{
enum class NetworkSessionResult : uint8_t
{
    Queued,
    Waiting,
    Progress,
    Rejected,
    Paused,
    Confirmed,
    StorageFailure,
    Conflict,
    Invalid
};
// One borrowed, serialized job execution context. The runtime owns the verified
// manifest, immutable KML, identity/media/directory leases, records and buffers.
// Bind after construction; unbind and wait for callbacks before destruction.
// No USB session dependency, queue, allocation or stored network frame here.
template <class Io, class Jobs, class Receipts, class Crypto, class Lease, class Sender, class Nonce>
class PublicationNetworkSession
{
  public:
    PublicationNetworkSession(Io& io, Jobs& jobs, Receipts& receipts, Crypto& crypto, Lease& lease, Sender& sender, Nonce& nonce,
                              PublicationJournal& current, PublicationJournal& next, PublicationJournal& verify,
                              const VerifiedManifestView& manifest, VerifiedReceiptView& receipt, geocaching::ByteView directory_public,
                              const uint8_t* local, const uint8_t* author, const char* kml,
                              uint8_t* wire, size_t wire_capacity, uint8_t* scratch, size_t scratch_capacity,
                              uint8_t* file_scratch, size_t file_capacity,
                              uint32_t response_ms, uint32_t backoff_ms, uint16_t attempts)
        : io_(io), jobs_(jobs), lease_(lease), sender_(sender), nonce_(nonce), current_(current), next_(next), verify_(verify), manifest_(manifest),
          receipts_(receipts), crypto_(crypto), receipt_(receipt), directory_public_(directory_public),
          upload_(jobs, lease, current, next, verify, local, scratch, scratch_capacity),
          publish_(jobs, receipts, crypto, lease, current, next, verify, receipt, directory_public, local, uint8_t(manifest.manifest.state), scratch, scratch_capacity),
          author_(author), kml_(kml), wire_(wire), scratch_(scratch), file_(file_scratch), wire_capacity_(wire_capacity), scratch_capacity_(scratch_capacity),
          file_capacity_(file_capacity), response_ms_(response_ms), backoff_ms_(backoff_ms), attempts_(attempts), binding_{dispatch, this, nullptr, nullptr, author, current.generation} {}
    const chat::lxmf::TrackDeliveryBinding& binding() const { return binding_; }
    bool directoryCurrent() const { return publish_.directoryCurrent(); }
    // Runtime invokes under the same serialization as inbound deliveries.
    PublicationStoreResult cancel(uint32_t generation, geocaching::ByteView author_hash)
    {
        if (!validBuffers() || !publicationManifestMatches(current_, manifest_) || !lease_.holds(current_)) return PublicationStoreResult::Conflict;
        const auto result = cancelPublicationBeforePublish(jobs_, current_, next_, verify_, generation, author_hash, scratch_, scratch_capacity_);
        if (result == PublicationStoreResult::Ready || result == PublicationStoreResult::StorageFailure) pause();
        return result;
    }
    bool dispatchRejected(uint64_t now) { return publishing_ ? window_.dispatchRejected(publish_, now) : window_.dispatchRejected(upload_, now); }
    NetworkSessionResult advance(uint64_t now)
    {
        if (!validBuffers()) return NetworkSessionResult::Invalid;
        if (!publicationManifestMatches(current_, manifest_) || !lease_.holds(current_)) return NetworkSessionResult::Conflict;
        if (current_.phase == PublicationPhase::Confirmed) return NetworkSessionResult::Confirmed;
        if (paused_) return NetworkSessionResult::Paused;
        if (current_.phase != PublicationPhase::Accepted && current_.phase != PublicationPhase::Uploading &&
            current_.phase != PublicationPhase::FileStored && current_.phase != PublicationPhase::Publishing) return NetworkSessionResult::Conflict;
        if (!window_.request().size)
        {
            publishing_ = current_.phase == PublicationPhase::FileStored || current_.phase == PublicationPhase::Publishing;
            if (!publishing_ && !nonce_.fill(seed_, sizeof(seed_))) return NetworkSessionResult::Invalid;
            if (!window_.begin(publishing_ ? geocaching::ByteView{current_.operation, 16} : geocaching::ByteView{seed_, 16}, now, response_ms_, backoff_ms_, attempts_)) return NetworkSessionResult::Invalid;
        }
        const auto poll = publishing_ ? window_.poll(publish_, now) : window_.poll(upload_, now);
        if (poll == ExchangePoll::Completed) return NetworkSessionResult::Progress;
        if (poll == ExchangePoll::Waiting) return NetworkSessionResult::Waiting;
        if (poll == ExchangePoll::Exhausted || poll == ExchangePoll::Paused) return NetworkSessionResult::Paused;
        if (poll != ExchangePoll::Ready) return NetworkSessionResult::Invalid;
        if (publishing_)
        {
            const auto sent = sendTrackPublicationStep(jobs_, publish_, sender_, current_, next_, verify_, manifest_, author_, wire_, wire_capacity_, scratch_, scratch_capacity_);
            if (sent == PublicationSendResult::Queued || sent == PublicationSendResult::Rejected)
            {
                if (!window_.sent(sent == PublicationSendResult::Queued, now)) return NetworkSessionResult::Invalid;
                return sent == PublicationSendResult::Queued ? NetworkSessionResult::Queued : NetworkSessionResult::Rejected;
            }
            window_.pause(publish_);
            return sent == PublicationSendResult::StorageFailure ? NetworkSessionResult::StorageFailure : NetworkSessionResult::Conflict;
        }
        const auto sent = sendTrackUploadStep(io_, upload_, sender_, current_, kml_, window_.request(), author_, wire_, wire_capacity_, file_, file_capacity_);
        if (sent == UploadSendResult::Queued || sent == UploadSendResult::Rejected)
        {
            if (!window_.sent(sent == UploadSendResult::Queued, now)) return NetworkSessionResult::Invalid;
            return sent == UploadSendResult::Queued ? NetworkSessionResult::Queued : NetworkSessionResult::Rejected;
        }
        window_.pause(upload_);
        return sent == UploadSendResult::StorageFailure ? NetworkSessionResult::StorageFailure : NetworkSessionResult::Conflict;
    }
    void pause()
    {
        paused_ = true;
        if (publishing_) window_.pause(publish_);
        else window_.pause(upload_);
    }
    bool resume(uint64_t now)
    {
        if (current_.phase == PublicationPhase::Cancelled || current_.phase == PublicationPhase::Failed || current_.phase == PublicationPhase::Confirmed) return false;
        if (window_.request().size)
        {
            if (!window_.resume(now)) return false;
        }
        else if (!paused_) return false;
        paused_ = false;
        return true;
    }

  private:
    bool validBuffers() const
    {
        if (!author_ || !kml_ || !*kml_ || !wire_ || wire_capacity_ < 4300 || !scratch_ || scratch_capacity_ < 1100 ||
            !file_ || file_capacity_ < 256 || !response_ms_ || !backoff_ms_ || !attempts_) return false;
        const geocaching::ByteView buffers[] = {{wire_, wire_capacity_}, {scratch_, scratch_capacity_}, {file_, file_capacity_}};
        const geocaching::ByteView inputs[] = {
            {reinterpret_cast<const uint8_t*>(this), sizeof(*this)}, {reinterpret_cast<const uint8_t*>(&current_), sizeof(current_)}, {reinterpret_cast<const uint8_t*>(&next_), sizeof(next_)}, {reinterpret_cast<const uint8_t*>(&verify_), sizeof(verify_)}, {reinterpret_cast<const uint8_t*>(&manifest_), sizeof(manifest_)}, manifest_.manifest.raw, manifest_.signature, {author_, 64}, {reinterpret_cast<const uint8_t*>(kml_), std::strlen(kml_) + 1}, {reinterpret_cast<const uint8_t*>(&io_), sizeof(io_)}, {reinterpret_cast<const uint8_t*>(&jobs_), sizeof(jobs_)}, {reinterpret_cast<const uint8_t*>(&sender_), sizeof(sender_)}, {reinterpret_cast<const uint8_t*>(&nonce_), sizeof(nonce_)}, {reinterpret_cast<const uint8_t*>(&lease_), sizeof(lease_)}};
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = i + 1; j < 3; ++j)
                if (signingSpansOverlap(buffers[i].data, buffers[i].size, buffers[j].data, buffers[j].size)) return false;
            for (const auto input : inputs)
                if (input.size && signingSpansOverlap(buffers[i].data, buffers[i].size, input.data, input.size)) return false;
            for (const auto input : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&receipts_), sizeof(receipts_)},
                                     {reinterpret_cast<const uint8_t*>(&crypto_), sizeof(crypto_)},
                                     {reinterpret_cast<const uint8_t*>(&receipt_), sizeof(receipt_)},
                                     directory_public_})
                if (input.size && signingSpansOverlap(buffers[i].data, buffers[i].size, input.data, input.size)) return false;
        }
        return true;
    }
    static bool dispatch(const chat::lxmf::CustomDeliveryView& view, void* context)
    {
        if (!context) return false;
        auto& session = *static_cast<PublicationNetworkSession*>(context);
        if (!session.lease_.holds(session.current_)) return false;
        const auto& receiver = session.publishing_ ? session.publish_.binding() : session.upload_.binding();
        return receiver.handler && receiver.handler(view, receiver.context);
    }
    Io& io_;
    Jobs& jobs_;
    Lease& lease_;
    Sender& sender_;
    Nonce& nonce_;
    PublicationJournal& current_;
    PublicationJournal& next_;
    PublicationJournal& verify_;
    const VerifiedManifestView& manifest_;
    Receipts& receipts_;
    Crypto& crypto_;
    VerifiedReceiptView& receipt_;
    geocaching::ByteView directory_public_;
    UploadReplyReceiver<Jobs, Lease> upload_;
    PublicationReplyReceiver<Jobs, Receipts, Crypto, Lease> publish_;
    RequestRetryWindow window_;
    const uint8_t* author_;
    const char* kml_;
    uint8_t* wire_;
    uint8_t* scratch_;
    uint8_t* file_;
    size_t wire_capacity_, scratch_capacity_, file_capacity_;
    uint32_t response_ms_, backoff_ms_;
    uint16_t attempts_;
    chat::lxmf::TrackDeliveryBinding binding_;
    uint8_t seed_[16]{};
    bool publishing_ = false;
    bool paused_ = false;
};
} // namespace track_sharing
