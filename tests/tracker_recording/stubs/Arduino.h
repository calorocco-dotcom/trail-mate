#pragma once
#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>
inline std::atomic<uint32_t> test_clock{10000};
inline uint32_t millis() { return test_clock.load(); }
#if defined(_WIN32)
inline std::tm* gmtime_r(const std::time_t* input, std::tm* output)
{
    // Recorder timestamp calls are serialized by its mutex in this harness.
    const auto* parsed = std::gmtime(input);
    if (!parsed) return nullptr;
    *output = *parsed;
    return output;
}
#endif
class String : public std::string
{
  public:
    using std::string::string;
    bool isEmpty() const { return empty(); }
};
