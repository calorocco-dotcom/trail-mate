#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <map>
namespace host_worker
{
inline std::map<void*, size_t> allocations;
inline size_t live = 0, peak = 0;
inline bool refuse = false, deny_cancel_lock = false;
inline bool fail_allocation = false, fail_task_create = false, fail_mutex_create = false, deny_lock = false;
inline void (*task)(void*) = nullptr;
inline void* context = nullptr;
inline void (*delay_hook)() = nullptr;
inline uint64_t ticks = 0;
inline void run()
{
    assert(task);
    auto entry = task;
    task = nullptr;
    entry(context);
}
} // namespace host_worker
