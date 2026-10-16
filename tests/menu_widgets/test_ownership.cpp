#include "ui/menu/dashboard/dashboard_widget.h"
#include <cassert>
#include <cstdlib>

bool fail_external = false;
unsigned live = 0, releases = 0, destroyed = 0;
void* heap_caps_malloc(std::size_t bytes, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_external) return nullptr;
    auto* memory = std::malloc(bytes);
    if (memory) ++live;
    return memory;
}
void heap_caps_free(void* memory)
{
    if (memory)
    {
        assert(live > 0);
        --live;
        ++releases;
    }
    std::free(memory);
}
struct State : ui::menu::dashboard::WidgetLifetime
{
    char scratch[4096]{};
    ~State() { ++destroyed; }
};
int main()
{
    using namespace ui::menu::dashboard;
    lv_init();
    auto* display = lv_display_create(480, 222);
    assert(display);
    for (int cycle = 0; cycle < 100; ++cycle)
    {
        auto* first = create_widget_state<State>();
        auto* second = create_widget_state<State>();
        assert(first && second && first != second && live == 2);
        assert(platform::ui::psramUiRequired());
        auto* parent = lv_obj_create(lv_screen_active());
        auto a = own_widget(lv_obj_create(parent), first, nullptr);
        auto b = own_widget(lv_obj_create(parent), second, nullptr);
        first->scratch[0] = 42;
        auto* borrowed = lv_obj_create(a.root);
        lv_obj_add_event_cb(
            borrowed, [](lv_event_t* event)
            {
            assert(live == 2); // Parent state still backs its deleting children.
            assert(static_cast<State*>(lv_event_get_user_data(event))->scratch[0] == 42); },
            LV_EVENT_DELETE, first);
        fail_external = true;
        assert(create_widget_state<State>() == nullptr); // No RAM fallback.
        assert(live == 2);
        fail_external = false;
        lv_obj_delete(a.root);
        assert(live == 1 && lv_obj_is_valid(b.root));
        second->scratch[0] = 42; // Deleting one widget preserves another.
        lv_obj_delete(parent);
        assert(live == 0 && !platform::ui::psramUiRequired());
    }
    assert(destroyed == 200 && releases == 200);
}
