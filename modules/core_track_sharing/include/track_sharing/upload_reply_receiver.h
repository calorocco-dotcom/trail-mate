#pragma once
#include "chat/ports/track_delivery_binding.h"
#include "track_sharing/persist_upload_response.h"

namespace track_sharing
{
// Owned by the admitted job, normally in PSRAM. The router serializes callbacks;
// lease.holds() must check author/media/directory leases without reentering it.
// Remove binding from the router before destroying this object or its records.
template <class Store, class Lease>
class UploadReplyReceiver
{
  public:
    UploadReplyReceiver(Store& store, Lease& lease, PublicationJournal& current,
                        PublicationJournal& next, PublicationJournal& verify,
                        const uint8_t local[16], uint8_t* scratch, size_t capacity)
        : store_(store), lease_(lease), current_(current), next_(next), verify_(verify),
          scratch_(scratch), capacity_(capacity), binding_{dispatch, this}
    {
        if (local) std::memcpy(local_, local, 16);
    }
    const chat::lxmf::TrackDeliveryBinding& binding() const { return binding_; }
    bool pending() const { return pending_; }
    geocaching::ByteView token() const { return {token_, has_token_ ? 16U : 0U}; }
    uint16_t chunkLimit() const { return chunk_limit_; }

    // Arm before dispatch; a retry reuses the same outstanding request bytes.
    // A send rejection can cancelPending(), then arm a new request.
    bool arm(uint8_t operation, geocaching::ByteView request, size_t chunk_bytes = 0)
    {
        if (pending_ || !networkRequestId(request) || !networkRequestId({local_, 16}) ||
            signingSpansOverlap(request.data, request.size, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            !publicationJournalValid(current_) || !lease_.holds(current_)) return false;
        if (operation == 4)
        {
            if (current_.phase != PublicationPhase::Accepted && current_.phase != PublicationPhase::Uploading) return false;
        }
        else if (operation == 5)
        {
            if (current_.phase != PublicationPhase::Uploading || !has_token_ || !chunk_bytes ||
                chunk_bytes > chunk_limit_ || current_.offset > current_.file_bytes ||
                chunk_bytes > current_.file_bytes - current_.offset) return false;
        }
        else if (operation == 6)
        {
            if (current_.phase != PublicationPhase::Uploading || !has_token_ || current_.offset != current_.file_bytes) return false;
        }
        else return false;
        std::memcpy(request_, request.data, 16);
        operation_ = operation;
        offset_ = current_.offset;
        sent_bytes_ = uint16_t(chunk_bytes);
        pending_ = true;
        return true;
    }
    void cancelPending() { pending_ = false; }

  private:
    static bool dispatch(const chat::lxmf::CustomDeliveryView& view, void* context)
    {
        return context && static_cast<UploadReplyReceiver*>(context)->receive(view);
    }
    bool receive(const chat::lxmf::CustomDeliveryView& view)
    {
        if (!pending_ || !lease_.holds(current_) || !view.source.data || view.source.size != 16 ||
            std::memcmp(view.source.data, current_.directory, 16) || !view.destination.data || view.destination.size != 16 ||
            std::memcmp(view.destination.data, local_, 16) || !view.message_hash.data || view.message_hash.size != 32 ||
            !view.data.data || !view.data.size || view.data.size > 8192 ||
            signingSpansOverlap(view.data.data, view.data.size, reinterpret_cast<const uint8_t*>(this), sizeof(*this))) return false;
        const geocaching::ByteView response{view.data.data, view.data.size}, request{request_, 16};
        auto result = PublicationStoreResult::Invalid;
        if (operation_ == 4)
        {
            NetworkUploadBegin accepted;
            result = persistTrackUploadBegin(store_, current_, next_, verify_, response, request, accepted, scratch_, capacity_);
            if (result == PublicationStoreResult::Ready)
            {
                has_token_ = !accepted.sealed;
                if (has_token_) std::memcpy(token_, accepted.token.data, 16);
                else std::memset(token_, 0, 16);
                chunk_limit_ = accepted.chunk_bytes;
            }
        }
        else if (operation_ == 5)
            result = persistTrackUploadAck(store_, current_, next_, verify_, response, request, {token_, 16}, offset_, sent_bytes_, scratch_, capacity_);
        else if (operation_ == 6)
            result = persistTrackUploadSealed(store_, current_, next_, verify_, response, request, scratch_, capacity_);
        if (result != PublicationStoreResult::Ready) return false;
        pending_ = false;
        return true;
    }
    Store& store_;
    Lease& lease_;
    PublicationJournal& current_;
    PublicationJournal& next_;
    PublicationJournal& verify_;
    uint8_t* scratch_;
    size_t capacity_;
    chat::lxmf::TrackDeliveryBinding binding_;
    uint8_t local_[16]{}, request_[16]{}, token_[16]{};
    uint32_t offset_ = 0;
    uint16_t sent_bytes_ = 0, chunk_limit_ = 0;
    uint8_t operation_ = 0;
    bool pending_ = false, has_token_ = false;
};
} // namespace track_sharing
