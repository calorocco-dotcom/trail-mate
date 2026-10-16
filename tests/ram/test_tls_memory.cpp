#include "esp_heap_caps.h"
#include "mbedtls/platform.h"
#include "platform/esp/arduino_common/tls_memory.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>
int main()
{
    assert(platform::esp::arduino_common::memory::initializeTlsMemoryPolicy());
    assert(installed_calloc && installed_free && live_bytes == 0);
    for (size_t bytes : {size_t(1), size_t(32), size_t(1023), size_t(1024), size_t(16384)})
    {
        auto* memory = static_cast<uint8_t*>(installed_calloc(1, bytes));
        assert(memory && live_bytes == bytes);
        for (size_t i = 0; i < bytes; ++i) assert(memory[i] == 0);
        installed_free(memory);
        assert(live_bytes == 0);
        allocation_fails = true;
        const auto before = allocations;
        assert(!installed_calloc(1, bytes) && allocations == before + 1); // No internal retry.
        allocation_fails = false;
    }
    const auto before = allocations;
    assert(!installed_calloc(0, 2) && !installed_calloc(2, 0));
    assert(!installed_calloc(std::numeric_limits<size_t>::max(), 2) && allocations == before);
    installed_free(nullptr);
    psram_available = false;
    void* internal = installed_calloc(1, 32);
    assert(internal && live_bytes == 32);
    installed_free(internal);
    assert(live_bytes == 0);
}
