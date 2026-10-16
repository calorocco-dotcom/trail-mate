#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void* heap_caps_calloc(std::size_t count, std::size_t bytes, uint32_t caps);
void heap_caps_free(void* memory);
