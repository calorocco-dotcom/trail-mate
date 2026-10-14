#pragma once
#include <atomic>

namespace platform::ui::tracker
{
class TrackerAccessGate
{
  public:
    bool tryAcquire() { return !held_.test_and_set(std::memory_order_acquire); }
    void release() { held_.clear(std::memory_order_release); }

  private:
    std::atomic_flag held_ = ATOMIC_FLAG_INIT;
};
static_assert(sizeof(TrackerAccessGate) == 1, "Tracker gate must stay a single lock-free flag");

struct TrackerControlState
{
    TrackerAccessGate access;
    std::atomic_bool stop_requested{false};
};
static_assert(sizeof(TrackerControlState) == 2, "Tracker control flags must share one compact object");

class TrackerGateScope
{
  public:
    explicit TrackerGateScope(TrackerAccessGate& gate) : gate_(gate.tryAcquire() ? &gate : nullptr) {}
    ~TrackerGateScope() { release(); }
    TrackerGateScope(const TrackerGateScope&) = delete;
    TrackerGateScope& operator=(const TrackerGateScope&) = delete;
    explicit operator bool() const { return gate_ != nullptr; }
    void release()
    {
        if (gate_)
        {
            gate_->release();
            gate_ = nullptr;
        }
    }

  private:
    TrackerAccessGate* gate_;
};
} // namespace platform::ui::tracker
