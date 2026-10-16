#include "platform/audio/media_volume.h"
#include <cassert>
int main()
{
    using namespace platform::audio;
    assert(media_volume(45) == 45);
    publish_media_volume(27);
    unsigned observed = 0;
    observe_media_volume([](void* context, uint8_t value)
                         { *static_cast<unsigned*>(context) = value; },
                         &observed);
    assert(observed == 27 && media_volume(45) == 27);
    publish_media_volume(80);
    assert(observed == 80 && media_volume(45) == 80);
    publish_media_volume(255);
    assert(observed == 100 && media_volume(45) == 100);
    observe_media_volume(nullptr, nullptr);
}
