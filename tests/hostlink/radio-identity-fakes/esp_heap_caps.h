#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
constexpr uint32_t MALLOC_CAP_8BIT = 1, MALLOC_CAP_SPIRAM = 2;
namespace radio_heap_fixture
{
inline size_t external_size = 8 * 1024 * 1024;
inline bool fail = false;
inline unsigned attempts = 0, allocations = 0, frees = 0;
inline uint32_t last_caps = 0;
inline std::map<void*, size_t> blocks;
} // namespace radio_heap_fixture
inline size_t heap_caps_get_total_size(uint32_t) { return radio_heap_fixture::external_size; }
inline void* heap_caps_malloc(size_t size, uint32_t caps)
{
    using namespace radio_heap_fixture;
    ++attempts;
    last_caps = caps;
    if (fail) return nullptr;
    auto* memory = std::malloc(size);
    if (memory)
    {
        blocks[memory] = size;
        ++allocations;
    }
    return memory;
}
inline void heap_caps_free(void* memory)
{
    if (!memory) return;
    using namespace radio_heap_fixture;
    const auto size = blocks.at(memory);
    for (size_t i = 0; i < size; ++i) assert(!static_cast<const uint8_t*>(memory)[i]);
    blocks.erase(memory);
    ++frees;
    std::free(memory);
}
