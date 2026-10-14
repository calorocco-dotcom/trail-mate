#pragma once
#include "chat/ports/track_binding_registration.h"
#include "track_sharing/read_reply_receiver.h"

namespace track_sharing
{
enum class ReadExchangeState : uint8_t
{
    Idle,
    Waiting,
    Ready,
    Rejected,
    Expired,
    Stale,
    Cancelled
};

// One admitted operation. Owner allocates this in PSRAM and lends a media/USB
// lease and synchronous validated SD sink. No response buffer, task or queue.
// Adapter follows IMeshAdapter's serialized binding/visitor contract. A live
// binding MUST be detached successfully before this object or sink is freed.
template <class Adapter, class Sink, class Lease>
class ReadNetworkExchange
{
  public:
    ReadNetworkExchange(Adapter& adapter, Sink& sink, Lease& lease, geocaching::ByteView author, uint32_t generation)
        : adapter_(adapter), lease_(lease), receiver_(sink, lease, {author_, 64}, generation), generation_(generation)
    {
        if (author.data && author.size == 64 && generation) std::memcpy(author_, author.data, 64);
        else generation_ = 0;
    }
    ReadNetworkExchange(const ReadNetworkExchange&) = delete;
    ReadNetworkExchange& operator=(const ReadNetworkExchange&) = delete;

    // Wire comes from the operation-specific read encoders. Header correlation
    // is checked again before immutable request bytes are admitted.
    chat::TrackBindingChange attach(uint8_t operation, geocaching::ByteView request, geocaching::ByteView directory,
                                    geocaching::ByteView local, geocaching::ByteView wire, uint16_t budget,
                                    uint64_t now, const chat::lxmf::TrackDeliveryBinding* previous = nullptr)
    {
        if (attached_ || state_ != ReadExchangeState::Idle || !generation_ || !wire.data || !wire.size || wire.size > sizeof(wire_) ||
            !networkInputDisjoint(wire, reinterpret_cast<uint8_t*>(this), sizeof(*this)) || !header(operation, request, wire, budget) ||
            !receiver_.arm(operation, request, directory, local, budget)) return chat::TrackBindingChange::Conflict;
        std::memcpy(directory_, directory.data, 16);
        std::memcpy(wire_, wire.data, wire.size);
        bytes_ = uint16_t(wire.size);
        began_ = now;
        state_ = ReadExchangeState::Waiting;
        const auto changed = adapter_.replaceTrackDelivery(previous, &receiver_.binding());
        if (changed == chat::TrackBindingChange::Applied) attached_ = true;
        else
        {
            receiver_.cancel();
            state_ = ReadExchangeState::Idle;
            bytes_ = 0;
        }
        return changed;
    }

    // Visits refresh the author guard under the existing router lock. Radio
    // sends happen only after returning from that lock; all bytes stay owned.
    chat::TrackBindingChange advance(uint64_t now, ReadExchangeState& output)
    {
        if (!attached_)
        {
            output = state_;
            return chat::TrackBindingChange::Conflict;
        }
        now_ = now;
        const auto changed = adapter_.visitTrackDelivery(&receiver_.binding(), current_author_, current_generation_, drive, this);
        if (changed != chat::TrackBindingChange::Applied) return changed;
        if (send_)
        {
            send_ = false;
            const auto sent = adapter_.sendTrackData(directory_, chat::lxmf::ByteSpan{wire_, bytes_}, author_, generation_, false, nullptr);
            if (!sent.ok)
            {
                rejected_ = true;
                const auto reconciled = adapter_.visitTrackDelivery(&receiver_.binding(), current_author_, current_generation_, drive, this);
                if (reconciled != chat::TrackBindingChange::Applied) return reconciled;
            }
        }
        output = state_;
        return chat::TrackBindingChange::Applied;
    }
    chat::TrackBindingChange detach()
    {
        if (!attached_) return chat::TrackBindingChange::Applied;
        const auto changed = adapter_.replaceTrackDelivery(&receiver_.binding(), nullptr);
        if (changed == chat::TrackBindingChange::Applied || changed == chat::TrackBindingChange::Conflict)
        {
            attached_ = false;
            receiver_.cancel();
            send_ = false;
            if (state_ == ReadExchangeState::Waiting) state_ = ReadExchangeState::Cancelled;
        }
        return changed;
    }
    bool attached() const { return attached_; }
    const chat::lxmf::TrackDeliveryBinding* binding() const { return &receiver_.binding(); }

  private:
    static bool header(uint8_t operation, geocaching::ByteView request, geocaching::ByteView wire, uint16_t budget)
    {
        if (!readResponseOperation(operation) || !networkRequestId(request) || budget < 512 || budget > 8192) return false;
        geocaching::protocol::CmpReader reader(wire);
        size_t fields = 0;
        uint64_t version = 0, role = 0, op = 0, limit = 0;
        geocaching::ByteView id;
        return reader.array(fields, 6) && fields == 6 && reader.unsignedInteger(version) && version == 1 &&
               reader.unsignedInteger(role) && role == 0 && reader.unsignedInteger(op) && op == operation &&
               reader.binary(id, 16) && id.size == 16 && !std::memcmp(id.data, request.data, 16) &&
               reader.unsignedInteger(limit) && limit == budget && reader.position() < wire.size;
    }
    static void drive(void* value)
    {
        auto& self = *static_cast<ReadNetworkExchange*>(value);
        self.send_ = false;
        if (self.state_ != ReadExchangeState::Waiting) return;
        if (!self.lease_.holds() || self.current_generation_ != self.generation_ || std::memcmp(self.current_author_, self.author_, 64))
            self.state_ = ReadExchangeState::Stale;
        else if (!self.receiver_.pending()) self.state_ = ReadExchangeState::Ready;
        else if (self.now_ < self.began_ || self.now_ - self.began_ >= 45000) self.state_ = ReadExchangeState::Expired;
        else if (self.rejected_) self.state_ = ReadExchangeState::Rejected;
        else if (!self.sent_)
        {
            self.sent_ = true;
            self.send_ = true;
        }
        if (self.state_ != ReadExchangeState::Waiting && self.state_ != ReadExchangeState::Ready) self.receiver_.cancel();
    }
    Adapter& adapter_;
    Lease& lease_;
    uint8_t author_[64]{}, current_author_[64]{}, directory_[16]{}, wire_[320]{};
    ReadReplyReceiver<Sink, Lease> receiver_;
    uint64_t began_ = 0, now_ = 0;
    uint32_t generation_ = 0, current_generation_ = 0;
    uint16_t bytes_ = 0;
    ReadExchangeState state_ = ReadExchangeState::Idle;
    bool attached_ = false, sent_ = false, send_ = false, rejected_ = false;
};
} // namespace track_sharing
