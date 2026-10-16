#pragma once
#include <cstdarg>
#include <cstdio>

namespace platform::ui::navigation
{
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
void
trace(const char* format, ...);
#else
inline void trace(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
}
#endif
} // namespace platform::ui::navigation
