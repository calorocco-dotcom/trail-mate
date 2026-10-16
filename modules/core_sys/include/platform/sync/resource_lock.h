#pragma once
#include <cstdint>
namespace platform::sync
{
// Fixed process-lifetime locks for tiny metadata updates. Implementations must
// keep their primitive internal on ESP. Never perform I/O or callbacks inside.
enum class Resource : uint8_t
{
    AudioFocus,
    MusicArtwork
};
void enter(Resource resource);
void leave(Resource resource);
class Guard
{
    Resource resource_;

  public:
    explicit Guard(Resource resource) : resource_(resource) { enter(resource_); }
    ~Guard() { leave(resource_); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
} // namespace platform::sync
