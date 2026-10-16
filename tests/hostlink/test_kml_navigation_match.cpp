#include "ui/support/kml_navigation_match.h"
#include "ui/support/kml_route_projection.h"
#include "ui/support/route_image_memory.h"
#include "ui/widgets/route_elevation_profile.h"
#include <cassert>
#include <cmath>
#include <string>
struct Point
{
    double lat = 0, lon = 0, altitude_m = 0;
    bool has_altitude = false, break_before = false;
};
using Points = ui::support::RouteImageVector<Point, 96>;
using Samples = ui::support::RouteImageVector<ui::widgets::route_elevation_profile::Sample, 96>;
namespace sys
{
uint32_t time = 100;
uint32_t millis_now() { return time; }
} // namespace sys
namespace platform::ui::device
{
bool ready = true;
bool sd_ready() { return ready; }
} // namespace platform::ui::device
enum class Projection
{
    Map,
    GpsStatus
};
void* s_root = reinterpret_cast<void*>(1);
void* s_location_request = nullptr;
void* s_map_target = nullptr;
Projection s_projection = Projection::Map;
bool s_route_deviation_active = false, fix_valid = true, route_active = true;
double s_route_deviation_distance_m = 0, fix_lat = 0, fix_lon = 0;
std::string s_track_file = "/route.kml";
ui::support::KmlNavigationMatch* s_route_navigation = nullptr;
constexpr double kRouteDeviationThresholdM = 10, kRouteDeviationClearThresholdM = 7;
bool current_fix_lat_lon(double& lat, double& lon)
{
    lat = fix_lat;
    lon = fix_lon;
    return fix_valid;
}
bool route_deviation_mode_active() { return route_active; }
// Native archive polling is an injected adapter in this KML-matching contract.
// The production dispatcher remains intact; native archive/session behavior has
// its own tests in tests/navigation and tests/navigation_ui.
namespace tmap
{
struct GeoPoint
{
    int32_t latitude = 0, longitude = 0;
};
} // namespace tmap
namespace platform::ui::gps
{
struct Fix
{
    bool valid = true;
    double lat = 0, lng = 0;
    uint32_t age = 0;
};
Fix data;
Fix get_data() { return data; }
} // namespace platform::ui::gps
namespace app
{
struct Config
{
    bool route_enabled = false;
    std::string route_path;
};
struct Facade
{
    Config config;
    const Config& readConfig() { return config; }
} facade;
Facade& configFacade() { return facade; }
} // namespace app
struct NativeRoute
{
    unsigned polls = 0;
    bool lost = false;
    void storageLost() { lost = true; }
    bool poll(bool, tmap::GeoPoint, uint32_t, uint32_t, bool, uint32_t (*)())
    {
        ++polls;
        return false;
    }
    bool ready() { return true; }
    tmap::GeoPoint destination() { return {10000000, 20000000}; }
};
NativeRoute* s_native_route = nullptr;
bool s_native_center_pending = false;
int s_map_zoom = 0, s_map_pan_x = 0, s_map_pan_y = 0;
constexpr int kDefaultTrackerZoom = 12;
unsigned native_sessions = 0, refreshes = 0;
void clear_native_session() { native_sessions = 0; }
void sync_native_viewport() {}
void request_refresh_view() { ++refreshes; }
void advance_native_session(tmap::GeoPoint, bool, uint32_t, uint32_t, bool) { ++native_sessions; }
struct Workspace
{
    struct Viewport
    {
        double center_lat = 0, center_lon = 0;
        int zoom = 0;
    } view;
    Viewport viewport() { return view; }
    bool setViewport(Viewport value)
    {
        view = value;
        return true;
    }
} workspace;
Workspace& map_workspace_model() { return workspace; }
#include "image_ui_navigation_match_state_actual.inc"
static std::string document(const std::string& body)
{
    return "<k:kml xmlns:k='http://www.opengis.net/kml/2.2' xmlns:g='http://www.google.com/kml/ext/2.2'><k:Placemark>" + body + "</k:Placemark></k:kml>";
}
static bool prepare(const std::string& body)
{
    ui::support::KmlNavigationMatch::destroy(s_route_navigation);
    s_route_navigation = ui::support::KmlNavigationMatch::create();
    if (!s_route_navigation) return false;
    kml_reader_fake::files["A:/route.kml"] = document(body);
    Points points;
    Samples samples;
    ui::support::KmlElevationMetrics metrics;
    return ui::support::projectKmlRoute("/route.kml", points, samples, metrics, 96, &s_route_navigation->expected);
}
static void complete(double lat, double lon)
{
    fix_lat = lat;
    fix_lon = lon;
    for (unsigned i = 0; i < 10000; ++i)
    {
        advance_route_navigation();
        if (!s_route_navigation->pending()) return;
        sys::time += 20;
    }
    assert(false);
}
static uint32_t counted_clock() { return sys::time++; }
int main()
{
    NativeRoute native;
    s_native_route = &native;
    advance_route_navigation();
    assert(native.polls == 1 && native_sessions == 1);
    platform::ui::device::ready = false;
    advance_route_navigation();
    assert(native.lost && native_sessions == 0 && native.polls == 1);
    platform::ui::device::ready = true;
    s_native_route = nullptr;
    using namespace track_sharing;
    RouteMatchAccumulator equator({.001, .005});
    assert(equator.point({0, 0}, true) && equator.point({0, .01}, false));
    const auto exact = equator.result();
    assert(exact.valid && !exact.ambiguous && std::abs(exact.distance_m - 111.1949) < .01 && std::abs(exact.progress_m - 555.9746) < .01);
    RouteMatchAccumulator date_line({0, 180});
    assert(date_line.point({0, 179.9}, true) && date_line.point({0, -179.9}, false));
    assert(date_line.result().distance_m < .001 && std::abs(date_line.result().total_m - 22238.985) < .01);
    RouteMatchAccumulator opposite({0, 0});
    assert(opposite.point({0, 179.9}, true) && opposite.point({0, -179.9}, false));
    assert(opposite.result().distance_m > 19000000); // no segment through longitude zero
    RouteMatchAccumulator ambiguous({0, 0});
    assert(ambiguous.point({0, 0}, true) && ambiguous.point({0, .001}, false) && ambiguous.point({0, 0}, false));
    assert(ambiguous.result().ambiguous);
    RouteMatchAccumulator invalid({NAN, 0});
    assert(!invalid.point({0, 0}, true));
    RouteMatchAccumulator antipodal;
    assert(antipodal.point({0, 0}, true) && !antipodal.point({0, 180}, false));

    RouteNavigationState state;
    RouteMatchResult position{0, 500, 1000, 500, true, false};
    state.observe(position, 100, 0);
    auto view = state.project(position, 0, 0);
    assert(view.status == RouteNavigationStatus::Position && view.percent == 50 && view.remaining_m == 500);
    assert(state.project(position, 5001, 0).status == RouteNavigationStatus::Unavailable);
    assert(state.project(position, 0, 3.01).status == RouteNavigationStatus::Unavailable);
    position.progress_m = 999;
    position.endpoint_distance_m = 1;
    state.observe(position, 1000, 0);
    state.observe(position, 1000, 0); // A rendered result is not another fix.
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Position);
    state.observe(position, 6000, 0);
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Arrived);
    assert(state.project(position, 0, 4).status == RouteNavigationStatus::Unavailable);
    position.ambiguous = true;
    state.observe(position, 6100, 0);
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Ambiguous);
    position.ambiguous = false;
    state.observe(position, 7000, 0);
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Position);
    state.observe(position, 20000, 0); // Missing scans restart the dwell.
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Position);
    state.reset();
    state.observe(position, 100, 0);
    state.observe(position, 5100, 0);
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Position); // Opening at the destination does not arrive.
    position.progress_m = 500;
    position.endpoint_distance_m = 500;
    state.observe(position, 0xffffe000U, 0);
    position.progress_m = 999;
    position.endpoint_distance_m = 1;
    state.observe(position, 0xfffff000U, 0);
    state.observe(position, 0x400U, 0);
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Arrived); // millis rollover
    position.total_m = 0;
    assert(state.project(position, 0, 0).status == RouteNavigationStatus::Unavailable);

    std::string curve = "<k:LineString><k:coordinates>";
    for (unsigned i = 0; i < 1000; ++i) curve += std::to_string(i * .00001) + "," + (i == 5 ? "0.001" : "0") + " ";
    curve += "</k:coordinates></k:LineString>";
    assert(prepare(curve));
    // The bend is between display samples; the matcher must see all raw points.
    const unsigned before = kml_reader_fake::reads;
    s_route_navigation->poll("/route.kml", .001, .00005, sys::time, counted_clock, 64, 4);
    assert(s_route_navigation->pending() && !s_route_navigation->result() && kml_reader_fake::reads - before <= 4);
    complete(.001, .00005);
    assert(s_route_navigation->result() && s_route_navigation->result()->distance_m < .001);
    update_route_deviation_state();
    assert(!s_route_deviation_active);
    assert(s_route_navigation->navigation(.001, .00005, sys::time).status == RouteNavigationStatus::Position);
    assert(s_route_navigation->navigation(.002, .00005, sys::time).status == RouteNavigationStatus::Unavailable);
    assert(image_fake::internal_allocations == 0 && kml_reader_fake::open_handles == 0);
    double lower = 0, upper = 0;
    assert(s_route_navigation->bounds(.002, .00005, sys::time, lower, upper) && lower == 0 && upper > 111);
    assert(!s_route_navigation->bounds(.001, .00005, sys::time + 15001, lower, upper));
    // Mutation after successful loading must not silently change the active route.
    kml_reader_fake::files["A:/route.kml"] = document("<k:LineString><k:coordinates>0,0 1,1</k:coordinates></k:LineString>");
    sys::time += 6000;
    complete(.001, .00005);
    assert(!s_route_navigation->result());
    assert(s_route_navigation->navigation(.001, .00005, sys::time).status == RouteNavigationStatus::Unavailable);

    assert(prepare("<k:MultiGeometry><k:LineString><k:coordinates>0,0 .001,0</k:coordinates></k:LineString><k:LineString><k:coordinates>1,0 1.001,0</k:coordinates></k:LineString></k:MultiGeometry>"));
    complete(0, .5);
    update_route_deviation_state();
    assert(s_route_navigation->result()->distance_m > 50000 && s_route_navigation->result()->total_m < 223 && s_route_deviation_active);
    // An uncertain interval cannot falsely clear a previous off-route alert.
    fix_lon = .001;
    update_route_deviation_state();
    assert(s_route_deviation_active);
    sys::time += 6000;
    complete(0, .001);
    update_route_deviation_state();
    assert(!s_route_deviation_active);

    assert(prepare("<k:LineString><k:coordinates>0,0 180,0</k:coordinates></k:LineString><g:Track><g:coord>1 0 0</g:coord><g:coord/><g:coord>2 0 0</g:coord></g:Track>"));
    complete(0, 1.5);
    assert(s_route_navigation->result() && s_route_navigation->result()->total_m == 0 && s_route_navigation->result()->distance_m > 55000);
    // Truncation may have emitted matching points, but can never publish Ready.
    kml_reader_fake::files["A:/route.kml"].resize(kml_reader_fake::files["A:/route.kml"].size() - 3);
    sys::time += 6000;
    complete(0, 1.5);
    assert(!s_route_navigation->result());

    assert(prepare("<k:LineString><k:coordinates>0,0 .001,0</k:coordinates></k:LineString>"));
    sys::time += 6000;
    // Stay inside the 10% integer interval; the exact 10% boundary can
    // evaluate just below ten with different floating-point implementations.
    complete(0, .000105);
    assert(s_route_navigation->navigation(0, .000105, sys::time).percent == 10);
    sys::time += 6000;
    complete(0, .000995);
    assert(s_route_navigation->navigation(0, .000995, sys::time).status == RouteNavigationStatus::Position);
    sys::time += 5000;
    complete(0, .000995);
    assert(s_route_navigation->navigation(0, .000995, sys::time).status == RouteNavigationStatus::Arrived);
    fix_valid = false;
    advance_route_navigation();
    assert(s_route_navigation->navigation(0, .000995, sys::time).status == RouteNavigationStatus::Unavailable);
    fix_valid = true;
    assert(prepare("<k:LineString><k:coordinates>0,0 .001,0 0,0</k:coordinates></k:LineString>"));
    sys::time += 6000;
    complete(0, 0);
    assert(s_route_navigation->navigation(0, 0, sys::time).status == RouteNavigationStatus::Ambiguous);

    assert(prepare(curve));
    s_route_navigation->poll("/route.kml", 0, 0, sys::time, nullptr, 1, 4);
    assert(s_route_navigation->pending());
    s_projection = Projection::GpsStatus;
    advance_route_navigation();
    assert(!s_route_navigation->pending() && !s_route_navigation->result() && !kml_reader_fake::open_handles);
    s_projection = Projection::Map;
    kml_reader_fake::fail_read = kml_reader_fake::reads + 1;
    complete(0, 0);
    assert(!s_route_navigation->result() && !kml_reader_fake::open_handles);
    kml_reader_fake::fail_read = -1;
    sys::time += 2000;
    kml_reader_fake::close_fail = true;
    complete(0, 0);
    assert(!s_route_navigation->result());
    kml_reader_fake::close_fail = false;
    sys::time += 2000;
    image_fake::allocation_fail = true;
    complete(0, 0);
    assert(!s_route_navigation->pending() && !s_route_navigation->result());
    image_fake::allocation_fail = false;
    fix_valid = false;
    advance_route_navigation();
    update_route_deviation_state();
    assert(!s_route_deviation_active);
    ui::support::KmlNavigationMatch::destroy(s_route_navigation);
    image_fake::allocation_fail = true;
    assert(!ui::support::KmlNavigationMatch::create());
    image_fake::allocation_fail = false;
    assert(!image_fake::live && !image_fake::internal_allocations && !kml_reader_fake::open_handles);
}
