#pragma once
#include "platform/esp/common/radio_identity_record.h"
#include <cstdlib>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif
namespace platform::esp::common
{
// Only the pointer is automatic. On PSRAM boards allocation failure never
// spills into internal heap. Boards with no PSRAM can use a bounded 144-byte
// internal allocation; every allocation is erased on exit.
class RadioIdentityWork
{
  public:
    RadioIdentityWork()
    {
#if defined(ESP_PLATFORM)
        const uint32_t caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT : MALLOC_CAP_8BIT;
        data_ = static_cast<uint8_t*>(heap_caps_malloc(kRadioIdentityWorkBytes, caps));
#else
        data_ = static_cast<uint8_t*>(std::malloc(kRadioIdentityWorkBytes));
#endif
    }
    ~RadioIdentityWork()
    {
        clearIdentityRecordWork(data_, kRadioIdentityWorkBytes);
#if defined(ESP_PLATFORM)
        heap_caps_free(data_);
#else
        std::free(data_);
#endif
    }
    RadioIdentityWork(const RadioIdentityWork&) = delete;
    RadioIdentityWork& operator=(const RadioIdentityWork&) = delete;
    uint8_t* data() const { return data_; }
    size_t size() const { return kRadioIdentityWorkBytes; }

  private:
    uint8_t* data_ = nullptr;
};
} // namespace platform::esp::common
