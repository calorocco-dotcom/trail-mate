#pragma once
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <cstddef>
#include <cstdint>
namespace platform::esp::route_images
{
// Caller owns source-file and route-storage lease. Output is caller-provided
// BGR24 pixels; no full decoded source image, no internal-heap fallback.
bool decodeRoutePng(arduino_common::storage::SdRuntimeFile& input,
                    uint8_t* bgr, size_t capacity, uint16_t width, uint16_t height);
} // namespace platform::esp::route_images
