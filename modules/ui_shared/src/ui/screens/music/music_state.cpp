#include "music_state.h"
#include <cstdlib>
#include <new>
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif
namespace ui::music_page
{
namespace
{
State* instance = nullptr;
}
State* state() { return instance; }
State* create_state()
{
#if defined(ESP_PLATFORM)
    auto* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    auto* memory = std::malloc(sizeof(State));
#endif
    instance = memory ? new (memory) State{} : nullptr;
    return instance;
}
void destroy_state()
{
    if (!instance) return;
    instance->~State();
#if defined(ESP_PLATFORM)
    heap_caps_free(instance);
#else
    std::free(instance);
#endif
    instance = nullptr;
}
} // namespace ui::music_page
