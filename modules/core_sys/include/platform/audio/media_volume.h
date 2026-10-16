#pragma once
#include <cstdint>
namespace platform::audio
{
// User media/alert gain is distinct from temporary call/codec session gain.
// Registration belongs to one process-lifetime playback adapter, never a view.
using MediaVolumeObserver = void (*)(void*, uint8_t);
void observe_media_volume(MediaVolumeObserver observer, void* context);
void publish_media_volume(uint8_t percent);
uint8_t media_volume(uint8_t fallback);
} // namespace platform::audio
