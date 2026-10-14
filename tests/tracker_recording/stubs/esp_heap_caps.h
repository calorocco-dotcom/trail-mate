#pragma once
#include <cstddef>
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline void* heap_caps_malloc_prefer(size_t bytes, size_t, ...) { return std::malloc(bytes); }
