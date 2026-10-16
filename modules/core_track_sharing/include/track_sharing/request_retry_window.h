#pragma once
#include "track_sharing/network_request.h"

namespace track_sharing
{
enum class ExchangePoll : uint8_t
{
    Ready,
    Waiting,
    Completed,
    Exhausted,
    Paused,
    Invalid
};
// One in-flight network operation, metadata only. Caller retains immutable wire
// inputs and exclusive receiver ownership. Rebuild retries with request(), never
// generate a replacement ID while the same exchange is unresolved. Receiver
// clears pending only after authenticated durable acceptance (or this window's
// timeout/pause cancellation). A successful send is not an acknowledgement.
class RequestRetryWindow
{
  public:
    bool begin(geocaching::ByteView request, uint64_t now, uint32_t response_ms, uint32_t backoff_ms, uint16_t attempts)
    {
        if (active_ || !networkRequestId(request) || !response_ms || !backoff_ms || !attempts ||
            !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(this), sizeof(*this))) return false;
        std::memcpy(request_, request.data, 16);
        deadline_ = last_ = now;
        response_ms_ = response_ms;
        backoff_ms_ = backoff_ms;
        attempts_ = 0;
        limit_ = attempts;
        active_ = true;
        queued_ = paused_ = ready_ = false;
        return true;
    }
    geocaching::ByteView request() const { return {request_, active_ ? 16U : 0U}; }
    uint16_t attempts() const { return attempts_; }
    template <class Receiver>
    ExchangePoll poll(Receiver& receiver, uint64_t now)
    {
        if (!active_ || now < last_) return ExchangePoll::Invalid;
        last_ = now;
        if (paused_) return ExchangePoll::Paused;
        if (queued_)
        {
            if (!receiver.pending())
            {
                active_ = ready_ = false;
                return ExchangePoll::Completed;
            }
            if (now < deadline_) return ExchangePoll::Waiting;
            receiver.cancelPending();
            queued_ = false;
            deadline_ = after(now, backoff_ms_);
        }
        if (attempts_ >= limit_)
        {
            ready_ = false;
            return ExchangePoll::Exhausted;
        }
        if (now < deadline_) return ExchangePoll::Waiting;
        ready_ = true;
        return ExchangePoll::Ready;
    }
    bool sent(bool accepted, uint64_t now)
    {
        if (!active_ || paused_ || !ready_ || now < last_ || attempts_ >= limit_) return false;
        last_ = now;
        ++attempts_;
        ready_ = false;
        queued_ = accepted;
        deadline_ = after(now, accepted ? response_ms_ : backoff_ms_);
        return true;
    }
    template <class Receiver>
    void pause(Receiver& receiver)
    {
        if (!active_) return;
        // The durable acknowledgement may arrive before the next scheduler
        // poll. Retire that exchange before pausing; its ID must not be reused
        // for the next chunk or protocol operation after resume.
        if (queued_ && !receiver.pending())
        {
            active_ = queued_ = ready_ = paused_ = false;
            return;
        }
        if (queued_) receiver.cancelPending();
        queued_ = ready_ = false;
        paused_ = true;
    }
    // Runtime staged a packet under the job lock, but actual dispatch outside
    // that lock was rejected. Keep the exact exchange ID and attempt count.
    template <class Receiver>
    bool dispatchRejected(Receiver& receiver, uint64_t now)
    {
        if (!active_ || !queued_ || paused_ || now < last_) return false;
        last_ = now;
        if (!receiver.pending())
        {
            active_ = queued_ = ready_ = false;
            return true;
        }
        receiver.cancelPending();
        queued_ = ready_ = false;
        deadline_ = after(now, backoff_ms_);
        return true;
    }
    bool resume(uint64_t now)
    {
        if (!active_ || queued_ || now < last_ || (!paused_ && attempts_ < limit_)) return false;
        last_ = deadline_ = now;
        attempts_ = 0;
        paused_ = ready_ = false;
        return true;
    }

  private:
    static uint64_t after(uint64_t now, uint32_t period) { return UINT64_MAX - now < period ? UINT64_MAX : now + period; }
    uint8_t request_[16]{};
    uint64_t deadline_ = 0, last_ = 0;
    uint32_t response_ms_ = 0, backoff_ms_ = 0;
    uint16_t attempts_ = 0, limit_ = 0;
    bool active_ = false, queued_ = false, paused_ = false, ready_ = false;
};
static_assert(sizeof(RequestRetryWindow) <= 56, "Retry window must not retain wire payloads");
} // namespace track_sharing
