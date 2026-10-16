#include "ui/components/dialog_frame.h"
#include "ui/page/page_profile.h"
#include "ui/support/native_navigation_controls.h"
#include <cassert>
#include <cstring>
#include <vector>
namespace ui::i18n
{
const char* tr(const char* key) { return key; }
void set_label_text(lv_obj_t* object, const char* text) { lv_label_set_text(object, text); }
void set_content_label_text_raw(lv_obj_t* object, const char* text) { lv_label_set_text(object, text); }
} // namespace ui::i18n
namespace ui::page_profile
{
const PageLayoutProfile& current()
{
    static PageLayoutProfile profile;
    profile.large_touch_hitbox = true;
    return profile;
}
} // namespace ui::page_profile
static lv_obj_t* button(lv_obj_t* parent)
{
    auto* object = lv_btn_create(parent);
    lv_obj_set_size(object, LV_PCT(100), 52);
    lv_label_create(object);
    return object;
}
int main()
{
    lv_init();
    auto* display = lv_display_create(320, 240);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, static_cast<uint32_t>(pixels.size() * 2), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    const auto frame = ui::components::dialog_frame::create(lv_screen_active(), {"Navigation", 304, 224});
    auto* group = lv_group_create();
    ui::support::NativeNavigationControls controls;
    controls.status = lv_label_create(frame.body);
    controls.close = frame.close;
    controls.replan = button(frame.body);
    controls.reload = button(frame.body);
    controls.automatic = lv_checkbox_create(frame.body);
    auto* stop = controls.createStop(frame.panel, "Stop navigation", nullptr, nullptr, true);
    lv_group_add_obj(group, controls.replan);
    lv_group_add_obj(group, frame.close);
    ui::support::NativeNavigationControls::View view;
    view.active = view.ready = view.session = true;
    controls.update(view, "/routes/navigation/original.tmroute");
    assert(!std::strcmp(lv_label_get_text(lv_obj_get_child(controls.replan, 0)), "Replan from here"));
    view.replanning = true;
    lv_group_focus_obj(controls.replan);
    controls.update(view, "/routes/navigation/original.tmroute");
    assert(!std::strcmp(lv_label_get_text(lv_obj_get_child(controls.replan, 0)), "Cancel replan"));
    assert(lv_group_get_focused(group) == frame.close);
    view.replanning = false;
    view.failed = true;
    controls.update(view, "/routes/navigation/original.tmroute");
    assert(!std::strcmp(lv_label_get_text(lv_obj_get_child(controls.replan, 0)), "Retry replan"));
    view.failed = false;
    view.arrived = true;
    view.active = false;
    controls.update(view, "/routes/navigation/original.tmroute");
    assert(lv_obj_has_state(controls.replan, LV_STATE_DISABLED));
    assert(lv_obj_has_flag(controls.automatic, LV_OBJ_FLAG_HIDDEN));
    assert(!std::strcmp(lv_label_get_text(lv_obj_get_child(stop, 0)), "Finish navigation"));
    lv_obj_update_layout(frame.panel);
    lv_area_t before{}, after{};
    lv_obj_get_coords(stop, &before);
    lv_obj_scroll_to_y(frame.body, 10000, LV_ANIM_OFF);
    lv_obj_update_layout(frame.panel);
    lv_obj_get_coords(stop, &after);
    assert(!std::memcmp(&before, &after, sizeof(before)));
    assert(before.y1 >= 0 && before.y2 < 240 && before.y2 - before.y1 + 1 >= 44);
    controls.reset();
    lv_group_delete(group);
    lv_obj_delete(frame.overlay);
    lv_display_delete(display);
}
