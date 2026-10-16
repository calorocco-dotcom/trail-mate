#include "ui/screens/music/music_page_shell.h"
#include "music_state.h"
#include "platform/ui/music_runtime.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/ui_common.h"
#include "ui/ui_theme.h"
#include <algorithm>

namespace ui::music_page
{
namespace
{
::music::IService* bound_service = nullptr;
void deleted(lv_event_t* event)
{
    auto* self = state();
    if (!self || lv_event_get_target(event) != self->root) return;
    if (self->timer) lv_timer_delete(self->timer);
    self->menu.reset();
    lv_obj_clean(self->root);
    lv_image_cache_drop(&self->cover_image);
    if (self->artwork)
    {
        self->artwork->drop();
        self->artwork = nullptr;
    }
    if (self->service) self->service->detach();
    destroy_state();
}
void tick(lv_timer_t*)
{
    auto* self = state();
    if (!self || !self->service || !self->service->snapshot(self->snapshot)) return;
    if (self->waiting_for_library && self->snapshot.library_revision == self->rendered_library) return;
    self->waiting_for_library = false;
    if ((self->view == View::Library || self->view == View::ChoosePlaylist) &&
        self->rendered_library != self->snapshot.library_revision)
        draw();
    else if (self->view == View::Details && self->rendered_track != self->snapshot.current.id)
        draw();
    else if (self->view == View::Details && self->rendered_metadata != self->snapshot.metadata_revision)
        draw();
    else update();
}
} // namespace
void bind_service(::music::IService* service) { bound_service = service; }
void enter(void* host, lv_obj_t* parent)
{
    exit(nullptr, nullptr);
    auto* self = create_state();
    if (!self)
    {
        lv_async_call([](void*)
                      {
            ui_request_exit_to_menu();
            ui::feedback::show_notice("Not enough PSRAM for Music", 2200); },
                      nullptr);
        return;
    }
    self->service = bound_service ? bound_service : platform::ui::music_service();
    self->host = static_cast<const ui::page::Host*>(host);
    self->wide = lv_display_get_horizontal_resolution(nullptr) >= 700;
    self->visible_rows = static_cast<uint8_t>(std::clamp<int32_t>((lv_display_get_vertical_resolution(nullptr) - 76) / 40, 1, 8));
    if (self->service)
    {
        self->service->attach(self->visible_rows);
        self->service->snapshot(self->snapshot);
    }
    self->view = ::music::has_session(self->snapshot) ? View::Playing : View::Library;
    self->root = lv_obj_create(parent);
    lv_obj_set_size(self->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(self->root, 0, 0);
    lv_obj_set_style_border_width(self->root, 0, 0);
    lv_obj_set_style_radius(self->root, 0, 0);
    lv_obj_set_style_bg_color(self->root, ui::theme::page_bg(), 0);
    lv_obj_clear_flag(self->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(self->root, deleted, LV_EVENT_DELETE, nullptr);
    set_default_group(app_g);
    ui::widgets::top_bar_init(self->bar, self->root);
    ui::widgets::top_bar_set_back_callback(self->bar, back, nullptr);
    lv_obj_add_event_cb(self->bar.back_btn, key_event, LV_EVENT_KEY, nullptr);
    self->body = lv_obj_create(self->root);
    lv_obj_update_layout(self->bar.container);
    const lv_coord_t top_height = lv_obj_get_height(self->bar.container);
    lv_obj_set_pos(self->body, 0, top_height);
    lv_obj_set_size(self->body, LV_PCT(100), lv_display_get_vertical_resolution(nullptr) - top_height - 24);
    lv_obj_set_style_pad_all(self->body, 0, 0);
    lv_obj_set_style_border_width(self->body, 0, 0);
    lv_obj_set_style_bg_opa(self->body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(self->body, 0, 0);
    lv_obj_clear_flag(self->body, LV_OBJ_FLAG_SCROLLABLE);
    self->footer = ui::components::bottom_bar::create(self->root);
    lv_obj_align(self->footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    self->timer = lv_timer_create(tick, 500, nullptr);
    draw();
}
void exit(void*, lv_obj_t*)
{
    if (auto* self = state(); self && self->root && lv_obj_is_valid(self->root)) lv_obj_delete(self->root);
}
} // namespace ui::music_page
