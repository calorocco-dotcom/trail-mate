#pragma once
#include <cstdint>
namespace chat
{
// Conflict is observed under the same callback-serialization lock as Applied:
// the expected context is no longer registered and its callbacks have retired.
enum class TrackBindingChange : uint8_t
{
    Applied,
    Conflict,
    Busy,
    Unsupported
};
using TrackBindingVisit = void (*)(void*);
} // namespace chat
