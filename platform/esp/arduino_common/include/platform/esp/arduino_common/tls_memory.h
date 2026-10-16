#pragma once

namespace platform::esp::arduino_common::memory
{
// Call once after board/PSRAM initialization, before starting network or crypto
// services. mbedTLS's allocator hooks are process-wide, not HTTP-client state.
bool initializeTlsMemoryPolicy();
} // namespace platform::esp::arduino_common::memory
