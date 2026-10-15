#include "ui/screens/tracker/tracker_navigation_dialog.h"
#include "platform/memory/psram_ptr.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/navigation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/components/dialog_frame.h"
#include "ui/components/map_poi_search.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace tracker::ui::navigation_dialog
{
namespace
{
namespace nav = platform::ui::navigation;
struct State
{
    nav::Request request{};
    nav::Snapshot snapshot{};
    lv_obj_t* root = nullptr;
    lv_obj_t* origin = nullptr;
    lv_obj_t* destination = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* calculate = nullptr;
    lv_obj_t* save = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* restore = nullptr;
    lv_timer_t* timer = nullptr;
    Completed completed = nullptr;
    void* context = nullptr;
    bool current_origin = true;
    bool destination_set = false;
    bool choosing_origin = false;
    bool calculating = false;
};
State* s_state = nullptr;
bool activate(lv_event_t* event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) return true;
    if (lv_event_get_code(event) != LV_EVENT_KEY || lv_event_get_key(event) != LV_KEY_ENTER) return false;
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}
void message(const char* key)
{
    if (s_state) ::ui::i18n::set_label_text(s_state->status, key);
}
void selected(const platform::ui::map_search::Result& result, void*)
{
    if (!s_state) return;
    auto& state = *s_state;
    auto& endpoint = state.choosing_origin ? state.request.origin : state.request.destination;
    endpoint.latitude_e7 = result.latitude_e7;
    endpoint.longitude_e7 = result.longitude_e7;
    std::memcpy(endpoint.name, result.name, sizeof(endpoint.name));
    endpoint.name[sizeof(endpoint.name) - 1] = 0;
    auto* label = lv_obj_get_child(state.choosing_origin ? state.origin : state.destination, 0);
    ::ui::i18n::set_content_label_text_raw(label, endpoint.name);
    if (state.choosing_origin) state.current_origin = false;
    else state.destination_set = true;
    lv_obj_add_state(state.save, LV_STATE_DISABLED);
}
void search(lv_event_t* event)
{
    if (!s_state || s_state->calculating || !activate(event)) return;
    auto& state = *s_state;
    state.choosing_origin = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)) == 0;
    const auto fix = platform::ui::gps::get_data();
    const double latitude = fix.valid ? fix.lat : state.request.origin.latitude_e7 / 1e7;
    const double longitude = fix.valid ? fix.lng : state.request.origin.longitude_e7 / 1e7;
    if (!::ui::components::map_poi_search::open(state.root, latitude, longitude, selected)) message("Search unavailable");
}
void current(lv_event_t* event)
{
    if (!s_state || s_state->calculating || !activate(event)) return;
    s_state->current_origin = true;
    ::ui::i18n::set_label_text(lv_obj_get_child(s_state->origin, 0), "Current location");
    lv_obj_add_state(s_state->save, LV_STATE_DISABLED);
}
void calculate(lv_event_t* event)
{
    if (!s_state || s_state->calculating || !activate(event)) return;
    auto& state = *s_state;
    if (!state.destination_set)
    {
        message("Choose destination");
        return;
    }
    if (state.current_origin)
    {
        const auto fix = platform::ui::gps::get_data();
        if (!fix.valid || fix.age > 5000 || !std::isfinite(fix.lat) || !std::isfinite(fix.lng) || std::abs(fix.lat) > 90 || std::abs(fix.lng) > 180)
        {
            message("No GPS fix");
            return;
        }
        state.request.origin.latitude_e7 = static_cast<int32_t>(std::llround(fix.lat * 1e7));
        state.request.origin.longitude_e7 = static_cast<int32_t>(std::llround(fix.lng * 1e7));
        std::snprintf(state.request.origin.name, sizeof(state.request.origin.name), "%s", ::ui::i18n::tr("Current location"));
    }
    nav::cancel();
    state.snapshot = {};
    if (!nav::submit(state.request))
    {
        message("Navigation unavailable");
        return;
    }
    state.calculating = true;
    lv_obj_add_state(state.calculate, LV_STATE_DISABLED);
    lv_obj_add_state(state.save, LV_STATE_DISABLED);
    message("Calculating route...");
}
void poll(lv_timer_t*)
{
    if (!s_state || !nav::poll(s_state->snapshot)) return;
    auto& state = *s_state;
    if (state.snapshot.status == nav::Status::Calculating) return;
    state.calculating = false;
    lv_obj_clear_state(state.calculate, LV_STATE_DISABLED);
    const char* key = "Navigation unavailable";
    switch (state.snapshot.status)
    {
    case nav::Status::Ready:
    {
        const auto text = ::ui::i18n::format("Route ready: %.1f km", state.snapshot.distance_m / 1000.0);
        ::ui::i18n::set_label_text_raw(state.status, text.c_str());
        lv_obj_clear_state(state.save, LV_STATE_DISABLED);
        return;
    }
    case nav::Status::NoRoutingData:
        key = "No TMAP routing data";
        break;
    case nav::Status::NoPath:
        key = "No path found";
        break;
    case nav::Status::OutsideCoverage:
        key = "Outside routing coverage";
        break;
    case nav::Status::MemoryUnavailable:
        key = "Navigation memory limit";
        break;
    case nav::Status::StorageUnavailable:
        key = "Route storage unavailable";
        break;
    case nav::Status::Cancelled:
        key = "Cancelled";
        break;
    default:
        break;
    }
    message(key);
}
void save(lv_event_t* event)
{
    if (!s_state || s_state->calculating || !activate(event)) return;
    if (!nav::save())
    {
        message("Save failed");
        return;
    }
    nav::poll(s_state->snapshot);
    const std::string path = s_state->snapshot.path;
    const auto completed = s_state->completed;
    void* context = s_state->context;
    close();
    if (completed) completed(path.c_str(), context);
}
void key(lv_event_t* event)
{
    if (lv_event_get_code(event) == LV_EVENT_KEY && (lv_event_get_key(event) == LV_KEY_ESC || lv_event_get_key(event) == LV_KEY_BACKSPACE))
    {
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        close();
    }
}
lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback, uintptr_t data = 0)
{
    auto* object = lv_btn_create(parent);
    ::ui::components::two_pane_styles::apply_btn_basic(object);
    lv_obj_set_size(object, LV_PCT(100), ::ui::page_profile::current().large_touch_hitbox ? 44 : 32);
    auto* label = lv_label_create(object);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, reinterpret_cast<void*>(data));
    lv_obj_add_event_cb(object, callback, LV_EVENT_KEY, reinterpret_cast<void*>(data));
    lv_obj_add_event_cb(object, key, LV_EVENT_KEY, nullptr);
    lv_group_add_obj(s_state->group, object);
    return object;
}
} // namespace
bool is_open() { return s_state != nullptr; }
bool open(lv_obj_t* parent, Completed completed, void* context)
{
    if (s_state || !parent || !nav::supported()) return false;
    s_state = ::platform::memory::createPsram<State>();
    if (!s_state) return false;
    auto& state = *s_state;
    state.completed = completed;
    state.context = context;
    state.restore = lv_group_get_default();
    state.group = lv_group_create();
    if (!state.group)
    {
        close();
        return false;
    }
    ::set_default_group(state.group);
    const auto frame = ::ui::components::dialog_frame::create(parent,
                                                              {"New navigation", lv_obj_get_width(parent), lv_obj_get_height(parent),
                                                               [](lv_event_t*)
                                                               { close(); },
                                                               key});
    state.root = frame.overlay;
    lv_group_add_obj(state.group, frame.close);
    auto* body = frame.body;
    auto* origin_title = lv_label_create(body);
    ::ui::i18n::set_label_text(origin_title, "Origin");
    state.origin = button(body, "Current location", search, 0);
    button(body, "Use current location", current);
    auto* origin_hint = lv_label_create(body);
    lv_obj_set_width(origin_hint, LV_PCT(100));
    lv_label_set_long_mode(origin_hint, LV_LABEL_LONG_WRAP);
    ::ui::i18n::set_label_text(origin_hint, "Tap the origin to search for a place");
    auto* destination_title = lv_label_create(body);
    ::ui::i18n::set_label_text(destination_title, "Destination");
    state.destination = button(body, "Choose destination", search, 1);
    state.status = lv_label_create(body);
    lv_obj_set_width(state.status, LV_PCT(100));
    lv_label_set_long_mode(state.status, LV_LABEL_LONG_WRAP);
    state.calculate = button(body, "Calculate route", calculate);
    state.save = button(body, "Save and preview", save);
    lv_obj_add_state(state.save, LV_STATE_DISABLED);
    state.timer = lv_timer_create(poll, 150, nullptr);
    platform::ui::gps::acquire_power_lease("navigation");
    lv_group_focus_obj(state.destination);
    return true;
}
void close()
{
    if (!s_state) return;
    ::ui::components::map_poi_search::close();
    auto* state = s_state;
    s_state = nullptr;
    nav::cancel();
    platform::ui::gps::release_power_lease("navigation");
    if (state->timer) lv_timer_del(state->timer);
    if (state->root && lv_obj_is_valid(state->root)) lv_obj_del(state->root);
    if (state->restore) ::set_default_group(state->restore);
    if (state->group) lv_group_del(state->group);
    ::platform::memory::destroyPsram(state);
}
} // namespace tracker::ui::navigation_dialog
