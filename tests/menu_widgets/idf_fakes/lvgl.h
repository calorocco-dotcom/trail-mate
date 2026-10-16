#pragma once
#include <cstddef>
#define LV_STDLIB_CUSTOM 99
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM
using lv_mem_pool_t = void*;
struct lv_mem_monitor_t
{
    std::size_t total_size, free_size;
};
enum lv_result_t
{
    LV_RESULT_OK
};
