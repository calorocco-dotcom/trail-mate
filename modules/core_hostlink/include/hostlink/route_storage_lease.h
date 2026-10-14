#pragma once
#include <atomic>
namespace hostlink::route_storage_lease
{
// One nonblocking lease for application-scheduler install/readback and UI
// deletion. No owner pointer is read from the UI thread and no mutex allocates.
inline std::atomic_flag held = ATOMIC_FLAG_INIT;
inline bool acquire() { return !held.test_and_set(std::memory_order_acquire); }
inline void release() { held.clear(std::memory_order_release); }
class Guard
{
  public:
    Guard() : acquired_(acquire()) {}
    ~Guard()
    {
        if (acquired_) release();
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    bool acquired() const { return acquired_; }

  private:
    bool acquired_;
};
} // namespace hostlink::route_storage_lease
