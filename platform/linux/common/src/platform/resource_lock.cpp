#include "platform/sync/resource_lock.h"
#include <atomic>
#include <mutex>
namespace platform::sync
{
#if defined(__MINGW32__) && !defined(_GLIBCXX_HAS_GTHREADS)
// The legacy Windows host-test compiler has no std::mutex. Production Linux
// uses scheduler-aware mutexes; ESP uses its own critical-section adapter.
namespace
{
std::atomic_flag locks[] = {ATOMIC_FLAG_INIT, ATOMIC_FLAG_INIT};
}
void enter(Resource resource)
{
    while (locks[static_cast<unsigned>(resource)].test_and_set(std::memory_order_acquire))
    {
    }
}
void leave(Resource resource) { locks[static_cast<unsigned>(resource)].clear(std::memory_order_release); }
#else
namespace
{
std::mutex locks[2];
}
void enter(Resource resource) { locks[static_cast<unsigned>(resource)].lock(); }
void leave(Resource resource) { locks[static_cast<unsigned>(resource)].unlock(); }
#endif
} // namespace platform::sync
