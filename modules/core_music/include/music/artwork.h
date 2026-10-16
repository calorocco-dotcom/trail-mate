#pragma once
#include "music/memory.h"
#include "platform/sync/resource_lock.h"
#include <cstdint>
namespace music
{
enum class CoverResult : uint8_t
{
    Ready,
    Missing,
    Invalid,
    Unsupported,
    TooLarge,
    OutOfMemory,
    Cancelled
};
namespace detail
{
// Xtensa native CAS cannot address external RAM. Keep the tiny synchronization
// primitive internal and the image/reference metadata in PSRAM.
struct ArtworkLock
{
    platform::sync::Guard guard{platform::sync::Resource::MusicArtwork};
};
} // namespace detail
enum class PictureFormat : uint8_t
{
    None,
    Jpeg,
    Png
};
// Immutable after publication. A service and each visible image hold separate references.
// The final release may run on either task; no LVGL object is owned by this object.
struct Artwork
{
    unsigned references = 1;
    uint64_t track_id = 0;
    uint16_t width = 0, height = 0;
    uint16_t* pixels = nullptr;
    void retain()
    {
        detail::ArtworkLock lock;
        ++references;
    }
    void drop()
    {
        bool last = false;
        {
            detail::ArtworkLock lock;
            last = --references == 0;
        }
        if (!last) return;
        release(pixels);
        this->~Artwork();
        release(this);
    }
};
inline Artwork* make_artwork(uint64_t id, uint16_t w, uint16_t h)
{
    if (!w || !h || w > 240 || h > 240) return nullptr;
    auto* art = make<Artwork>();
    if (!art) return nullptr;
    art->track_id = id;
    art->width = w;
    art->height = h;
    art->pixels = static_cast<uint16_t*>(allocate(static_cast<std::size_t>(w) * h * 2));
    if (!art->pixels)
    {
        art->drop();
        return nullptr;
    }
    return art;
}
} // namespace music
