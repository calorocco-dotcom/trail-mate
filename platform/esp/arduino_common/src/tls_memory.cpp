#include "platform/esp/arduino_common/tls_memory.h"

#include "esp_heap_caps.h"
#include "mbedtls/platform.h"
#include <cstdint>
#include <cstdio>
#include <limits>

namespace platform::esp::arduino_common::memory
{
namespace
{
void* tls_calloc(std::size_t count, std::size_t size)
{
    if (count == 0 || size == 0 || count > std::numeric_limits<std::size_t>::max() / size)
    {
        return nullptr;
    }
    // No internal fallback when PSRAM exists: failed handshakes must leave the
    // radio, RTOS and DMA budget available. Boards without PSRAM retain support.
    const uint32_t caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0
                              ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                              : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    return heap_caps_calloc(count, size, caps);
}
} // namespace

bool initializeTlsMemoryPolicy()
{
    const bool configured = mbedtls_platform_set_calloc_free(tls_calloc, heap_caps_free) == 0;
    std::printf("[Mem][TLS] startup allocator configured=%u psram=%u\n",
                configured ? 1U : 0U, heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? 1U : 0U);
    return configured;
}
} // namespace platform::esp::arduino_common::memory
