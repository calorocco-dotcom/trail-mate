#pragma once
#include "chat/ports/track_delivery_binding.h"
#include "track_sharing/network_read_response.h"

namespace track_sharing
{
// Sink.validateAndStore must synchronously validate the entire operation body
// and store required bytes before returning true. No borrowed response survives
// the callback. Owner supplies storage and must unregister before destruction.
template <class Sink, class Lease>
class ReadReplyReceiver
{
  public:
    ReadReplyReceiver(Sink& sink, Lease& lease, geocaching::ByteView author, uint32_t generation)
        : sink_(sink), lease_(lease)
    {
        binding_.control = dispatch;
        binding_.context = this;
        if (author.data && author.size == 64 && generation)
        {
            binding_.expected_local_author = author.data;
            binding_.expected_author_generation = generation;
        }
    }
    const chat::lxmf::TrackDeliveryBinding& binding() const { return binding_; }
    bool pending() const { return pending_; }
    bool arm(uint8_t operation, geocaching::ByteView request, geocaching::ByteView directory,
             geocaching::ByteView local, uint16_t budget)
    {
        if (pending_ || !readResponseOperation(operation) || !networkRequestId(request) ||
            !networkRequestId(directory) || !networkRequestId(local) || budget < 512 || budget > 8192 ||
            !binding_.expected_local_author || !lease_.holds() ||
            !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(this), sizeof(*this)) ||
            !networkInputDisjoint(directory, reinterpret_cast<uint8_t*>(this), sizeof(*this)) ||
            !networkInputDisjoint(local, reinterpret_cast<uint8_t*>(this), sizeof(*this))) return false;
        std::memcpy(request_, request.data, 16);
        std::memcpy(directory_, directory.data, 16);
        std::memcpy(local_, local.data, 16);
        operation_ = operation;
        budget_ = budget;
        accepted_ = false;
        pending_ = true;
        return true;
    }
    void cancel() { pending_ = accepted_ = false; }

  private:
    static bool dispatch(const chat::lxmf::CustomDeliveryView& view, void* context)
    {
        return context && static_cast<ReadReplyReceiver*>(context)->receive(view);
    }
    bool receive(const chat::lxmf::CustomDeliveryView& view)
    {
        if ((!pending_ && !accepted_) || !lease_.holds() || !view.source.data || view.source.size != 16 ||
            std::memcmp(view.source.data, directory_, 16) || !view.destination.data || view.destination.size != 16 ||
            std::memcmp(view.destination.data, local_, 16) || !view.message_hash.data || view.message_hash.size != 32 ||
            !view.data.data || !view.data.size || view.data.size > budget_ ||
            !networkInputDisjoint({view.data.data, view.data.size}, reinterpret_cast<uint8_t*>(this), sizeof(*this))) return false;
        if (accepted_) return !std::memcmp(message_hash_, view.message_hash.data, 32);
        ReadResponseView response;
        if (!decodeTrackReadEnvelope({view.data.data, view.data.size}, {request_, 16}, operation_, response) ||
            !sink_.validateAndStore(operation_, {request_, 16}, response, {view.data.data, view.data.size})) return false;
        std::memcpy(message_hash_, view.message_hash.data, 32);
        pending_ = false;
        accepted_ = true;
        return true;
    }
    Sink& sink_;
    Lease& lease_;
    chat::lxmf::TrackDeliveryBinding binding_{};
    uint8_t request_[16]{}, directory_[16]{}, local_[16]{}, message_hash_[32]{};
    uint16_t budget_ = 0;
    uint8_t operation_ = 0;
    bool pending_ = false, accepted_ = false;
};
} // namespace track_sharing
