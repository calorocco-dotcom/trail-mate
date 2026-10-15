#include "platform/ui/gps_runtime.h"
#include "platform/ui/navigation_runtime.h"
#include "ui/app_runtime.h"
#include "ui/components/map_poi_search.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/tracker/tracker_navigation_dialog.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace nav = platform::ui::navigation;
namespace dialog = tracker::ui::navigation_dialog;
static nav::Request requested;
static nav::Snapshot response;
static gps::GpsState fix;
static unsigned submitted = 0, leases = 0;
static ui::components::map_poi_search::Selection selection = nullptr;
static void* selection_context = nullptr;
static std::string saved_path;

namespace platform::ui::navigation
{
bool supported() { return true; }
bool submit(const Request& request)
{
    requested = request;
    ++submitted;
    response = {};
    response.status = Status::Calculating;
    response.revision = 1;
    return true;
}
bool poll(Snapshot& snapshot)
{
    if (snapshot.revision == response.revision) return false;
    snapshot = response;
    return true;
}
bool save()
{
    assert(response.status == Status::Ready);
    response.status = Status::Saved;
    std::strcpy(response.path, "/routes/navigation/nav-origin-ridge-12345678.kml");
    ++response.revision;
    return true;
}
void cancel() { response = {}; }
} // namespace platform::ui::navigation
namespace platform::ui::gps
{
GpsState get_data() { return fix; }
void acquire_power_lease(const char*) { ++leases; }
void release_power_lease(const char*)
{
    assert(leases);
    --leases;
}
} // namespace platform::ui::gps
namespace ui::components::map_poi_search
{
bool open(lv_obj_t*, double, double, Selection callback, void* context)
{
    selection = callback;
    selection_context = context;
    return true;
}
void close() { selection = nullptr; }
} // namespace ui::components::map_poi_search
namespace ui::i18n
{
const char* tr(const char* key) { return key; }
void set_label_text(lv_obj_t* object, const char* text) { lv_label_set_text(object, text); }
void set_label_text_raw(lv_obj_t* object, const char* text) { lv_label_set_text(object, text); }
void set_content_label_text_raw(lv_obj_t* object, const char* text) { lv_label_set_text(object, text); }
std::string format(const char* key, ...)
{
    char text[200];
    va_list args;
    va_start(args, key);
    std::vsnprintf(text, sizeof(text), key, args);
    va_end(args);
    return text;
}
} // namespace ui::i18n
namespace ui::page_profile
{
const PageLayoutProfile& current()
{
    static const auto profile = []
    { PageLayoutProfile value; value.large_touch_hitbox = true; return value; }();
    return profile;
}
} // namespace ui::page_profile
void set_default_group(lv_group_t* group) { lv_group_set_default(group); }

static lv_obj_t* find(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index)
        if (auto* result = find(lv_obj_get_child(root, index), text)) return result;
    return nullptr;
}
static void click(const char* text)
{
    auto* label = find(lv_screen_active(), text);
    assert(label);
    auto* button = lv_obj_get_parent(label);
    assert(lv_obj_check_type(button, &lv_button_class) && !lv_obj_has_state(button, LV_STATE_DISABLED));
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}
static void choose(const char* label, const char* name, int32_t latitude, int32_t longitude)
{
    click(label);
    assert(selection);
    platform::ui::map_search::Result result{};
    result.latitude_e7 = latitude;
    result.longitude_e7 = longitude;
    std::snprintf(result.name, sizeof(result.name), "%s", name);
    selection(result, selection_context);
    ui::components::map_poi_search::close();
}
static void tick()
{
    lv_tick_inc(200);
    lv_timer_handler();
}
static void completed(const char* path, void*) { saved_path = path; }
int main()
{
    lv_init();
    auto* display = lv_display_create(320, 240);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(d); });
    auto* original_group = lv_group_create();
    lv_group_set_default(original_group);
    assert(dialog::open(lv_screen_active(), completed));
    click("Calculate route");
    assert(submitted == 0);
    choose("Choose destination", "Ridge", 312010000, 1214100000);
    click("Calculate route");
    assert(submitted == 0 && find(lv_screen_active(), "No GPS fix"));
    fix.valid = true;
    fix.lat = 31.2;
    fix.lng = 121.4;
    click("Calculate route");
    assert(submitted == 1 && requested.origin.latitude_e7 == 312000000 && requested.origin.longitude_e7 == 1214000000);
    assert(requested.destination.latitude_e7 == 312010000 && requested.destination.longitude_e7 == 1214100000);
    response.status = nav::Status::Ready;
    response.distance_m = 1200;
    ++response.revision;
    tick();
    assert(find(lv_screen_active(), "Route ready: 1.2 km"));
    click("Save and preview");
    assert(!dialog::is_open() && saved_path == "/routes/navigation/nav-origin-ridge-12345678.kml");
    assert(lv_group_get_default() == original_group && leases == 0);

    fix = {};
    assert(dialog::open(lv_screen_active(), completed));
    choose("Current location", "Park", 300000000, 1200000000);
    choose("Choose destination", "Summit", 301000000, 1201000000);
    click("Calculate route");
    assert(submitted == 2 && requested.origin.latitude_e7 == 300000000 && std::strcmp(requested.origin.name, "Park") == 0);
    auto* cancel = lv_obj_get_parent(find(lv_screen_active(), "Cancel"));
    auto* overlay = lv_obj_get_parent(lv_obj_get_parent(cancel));
    auto* body = lv_obj_get_child(overlay, 1);
    lv_obj_update_layout(overlay);
    lv_area_t before{}, after{};
    lv_obj_get_coords(cancel, &before);
    lv_obj_scroll_to_y(body, 10000, LV_ANIM_OFF);
    lv_obj_update_layout(overlay);
    lv_obj_get_coords(cancel, &after);
    assert(std::memcmp(&before, &after, sizeof(before)) == 0 && before.y1 >= 0 && before.y2 < 240);
    click("Cancel");
    assert(!dialog::is_open() && leases == 0 && lv_group_get_default() == original_group);
    lv_group_delete(original_group);
    lv_display_delete(display);
    std::puts("Navigation dialog: current GPS, searched endpoints, ready/save/preview, fixed touch cancel PASS");
}
