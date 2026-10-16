#include "platform/audio/media_volume.h"
#include <atomic>
namespace platform::audio
{
namespace
{
std::atomic<uint8_t> value{255};
std::atomic<void*> observer_context{nullptr};
std::atomic<MediaVolumeObserver> observer_function{nullptr};
} // namespace
void observe_media_volume(MediaVolumeObserver observer, void* context)
{
    observer_context.store(context);
    observer_function.store(observer);
    const auto current = value.load();
    if (observer && current <= 100) observer(context, current);
}
void publish_media_volume(uint8_t percent)
{
    percent = percent > 100 ? 100 : percent;
    value.store(percent);
    const auto observer = observer_function.load();
    if (observer) observer(observer_context.load(), percent);
}
uint8_t media_volume(uint8_t fallback)
{
    const auto current = value.load();
    return current <= 100 ? current : fallback;
}
} // namespace platform::audio
