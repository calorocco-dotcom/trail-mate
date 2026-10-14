#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void* heap_caps_malloc(size_t, unsigned);
void heap_caps_free(void*);
inline size_t heap_caps_get_free_size(unsigned) { return 1048576; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return 1048576; }
