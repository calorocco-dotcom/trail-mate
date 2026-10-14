#pragma once
#include "chat/ports/track_delivery_binding.h"
#include "track_sharing/confirm_publication.h"
#include "track_sharing/network_publish.h"

namespace track_sharing
{
// The admitted owner supplies records/proof/scratch in PSRAM. Lease holds the
// independently authenticated directory public/delivery/epoch binding and
// immutable author/media/path leases through the callback, without router calls.
template <class Jobs, class Receipts, class Crypto, class Lease>
class PublicationReplyReceiver
{
  public:
    PublicationReplyReceiver(Jobs& jobs, Receipts& receipts, Crypto& crypto, Lease& lease,
                             PublicationJournal& current, PublicationJournal& next, PublicationJournal& verify,
                             VerifiedReceiptView& proof, geocaching::ByteView directory_public,
                             const uint8_t local[16], uint8_t state, uint8_t* scratch, size_t capacity)
        : jobs_(jobs), receipts_(receipts), crypto_(crypto), lease_(lease), current_(current), next_(next), verify_(verify),
          proof_(proof), directory_public_(directory_public), scratch_(scratch), capacity_(capacity), state_(state), binding_{dispatch, this}
    {
        if (local) std::memcpy(local_, local, 16);
    }
    const chat::lxmf::TrackDeliveryBinding& binding() const { return binding_; }
    bool pending() const { return pending_; }
    bool directoryCurrent() const { return directory_current_; }
    bool arm()
    {
        if (pending_ || !publicationJournalValid(current_) || current_.phase != PublicationPhase::Publishing || state_ > 1 ||
            !directory_public_.data || directory_public_.size != 64 || !networkRequestId({local_, 16}) || !lease_.holds(current_)) return false;
        pending_ = true;
        return true;
    }
    void cancelPending() { pending_ = false; }

  private:
    static bool dispatch(const chat::lxmf::CustomDeliveryView& view, void* context)
    {
        return context && static_cast<PublicationReplyReceiver*>(context)->receive(view);
    }
    bool receive(const chat::lxmf::CustomDeliveryView& view)
    {
        if (!pending_ || !lease_.holds(current_) || !view.source.data || view.source.size != 16 ||
            std::memcmp(view.source.data, current_.directory, 16) || !view.destination.data || view.destination.size != 16 ||
            std::memcmp(view.destination.data, local_, 16) || !view.message_hash.data || view.message_hash.size != 32 ||
            !view.data.data || !view.data.size || view.data.size > 8192 ||
            signingSpansOverlap(view.data.data, view.data.size, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            signingSpansOverlap(scratch_, capacity_, reinterpret_cast<const uint8_t*>(this), sizeof(*this))) return false;
        const auto* proof_bytes = reinterpret_cast<const uint8_t*>(&proof_);
        for (const auto span : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&current_), sizeof(current_)},
                                {reinterpret_cast<const uint8_t*>(&next_), sizeof(next_)},
                                {reinterpret_cast<const uint8_t*>(&verify_), sizeof(verify_)},
                                {reinterpret_cast<const uint8_t*>(this), sizeof(*this)},
                                directory_public_})
            if (signingSpansOverlap(proof_bytes, sizeof(proof_), span.data, span.size)) return false;
        PublishResponseView response;
        if (!decodeTrackPublishResponse({view.data.data, view.data.size}, {current_.operation, 16}, response) ||
            verifyReceipt(response.signed_receipt, current_, {current_.operation, 16}, directory_public_, state_, crypto_, scratch_, capacity_, proof_) != ReceiptVerification::Valid)
        {
            proof_ = {};
            return false;
        }
        const auto result = confirmPublication(jobs_, receipts_, proof_, current_, next_, verify_, {current_.operation, 16}, directory_public_, state_, scratch_, capacity_);
        proof_ = {}; // No transport-owned byte view survives this callback.
        if (result != PublicationStoreResult::Ready) return false;
        if (current_.phase != PublicationPhase::Confirmed) current_ = next_;
        directory_current_ = response.current;
        pending_ = false;
        return true;
    }
    Jobs& jobs_;
    Receipts& receipts_;
    Crypto& crypto_;
    Lease& lease_;
    PublicationJournal& current_;
    PublicationJournal& next_;
    PublicationJournal& verify_;
    VerifiedReceiptView& proof_;
    geocaching::ByteView directory_public_;
    uint8_t* scratch_;
    size_t capacity_;
    uint8_t local_[16]{};
    uint8_t state_ = 0;
    bool pending_ = false, directory_current_ = false;
    chat::lxmf::TrackDeliveryBinding binding_;
};
} // namespace track_sharing
