#pragma once
#include <atomic>
#include <cstdint>

namespace platform::esp::storage
{
// Replace a mounted boolean, preserving its one-byte footprint. Failure is
// separate from mounted status and only observed by an operation-scoped reader.
class MountIoState
{
  public:
    operator bool() const { return (flags_.load(std::memory_order_acquire) & 1) != 0; }
    MountIoState& operator=(bool mounted)
    {
        flags_.store(mounted ? 1 : 0, std::memory_order_release);
        return *this;
    }
    void markFailure() { flags_.fetch_or(2, std::memory_order_relaxed); }
    bool failed() const { return (flags_.load(std::memory_order_relaxed) & 2) != 0; }
    void clearFailure() { flags_.fetch_and(1, std::memory_order_relaxed); }

  private:
    std::atomic<uint8_t> flags_{0};
};
static_assert(sizeof(MountIoState) == sizeof(bool), "Mount I/O state must reuse the existing byte");
class MountIoErrorScope
{
  public:
    explicit MountIoErrorScope(MountIoState& state) : state_(state), previous_(state.failed()) { state_.clearFailure(); }
    ~MountIoErrorScope()
    {
        if (previous_) state_.markFailure();
    }
    MountIoErrorScope(const MountIoErrorScope&) = delete;
    MountIoErrorScope& operator=(const MountIoErrorScope&) = delete;
    bool error() const { return state_.failed(); }

  private:
    MountIoState& state_;
    bool previous_;
};
} // namespace platform::esp::storage
