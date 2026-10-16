#include "ui/support/kml_route_projection.h"
#include "ui/support/overlay_path_segments.h"
#include "ui/support/route_image_memory.h"
#include "ui/support/route_segment_sampling.h"
#include "ui/widgets/route_elevation_profile.h"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
namespace platform::ui::device
{
bool sd_ready() { return true; }
} // namespace platform::ui::device
constexpr size_t kMaxTrackOverlayPoints = 96;
constexpr double kEarthRadiusM = 6371000;
// clang-format off
#include "image_ui_navigation_route_structs_actual.inc"
TrackPointBuffer s_track_points;
enum class TrackOverlayFileKind { Route, Track };
bool s_track_overlay_active = true;
TrackOverlayFileKind s_track_overlay_kind = TrackOverlayFileKind::Route;
bool route_deviation_mode_active() { return s_track_overlay_active && !s_track_points.empty(); }
double degrees_to_radians(double d) { return d * 0.017453292519943295; }
#include "image_ui_navigation_route_projection_actual.inc"
double route_point_distance_m(const TrackOverlayPoint& a, const TrackOverlayPoint& b)
{
    const auto la = degrees_to_radians(a.lat), lb = degrees_to_radians(b.lat);
    const auto x = std::sin((lb-la)/2), y = std::sin(degrees_to_radians(b.lon-a.lon)/2);
    const auto h = std::clamp(x*x+std::cos(la)*std::cos(lb)*y*y,0.0,1.0);
    return 2*kEarthRadiusM*std::atan2(std::sqrt(h),std::sqrt(1-h));
}
#include "image_ui_navigation_route_load_actual.inc"
#include "image_ui_navigation_route_samples_actual.inc"
#include "image_ui_navigation_route_nearest_actual.inc"
#include "image_ui_navigation_route_overlay_actual.inc"
// clang-format on
static std::string document(const std::string& body)
{
    return "<k:kml xmlns:k='http://www.opengis.net/kml/2.2' xmlns:g='http://www.google.com/kml/ext/2.2'><k:Placemark>" + body + "</k:Placemark></k:kml>";
}
static bool load(const std::string& text, TrackPointBuffer& points, ElevationSampleBuffer& samples, TrackElevationMetrics& metrics)
{
    kml_reader_fake::files["A:/route.kml"] = text;
    return load_kml_track_points("/route.kml", points, metrics, samples);
}
int main()
{
    TrackPointBuffer points;
    ElevationSampleBuffer samples;
    TrackElevationMetrics metrics;
    const auto segmented = document("<k:MultiGeometry><k:LineString><k:altitudeMode>absolute</k:altitudeMode><k:coordinates>0,0,0 0.001,0,10</k:coordinates></k:LineString><k:LineString><k:coordinates>1,0,100 1.001,0,110</k:coordinates><k:altitudeMode>absolute</k:altitudeMode></k:LineString></k:MultiGeometry>");
    assert(load(segmented, points, samples, metrics));
    assert(points.size() == 4 && samples.size() == 4 && points[2].break_before && samples[2].break_before);
    assert(metrics.ascent_m == 20 && metrics.altitude_count == 4 && samples.back().distance_m < 225 && samples.back().distance_m > 220);
    s_track_points = std::move(points);
    double distance = 0;
    assert(nearest_route_distance_m(0, 0.5, distance) && distance > 50000); // gap is not a nearest segment
    ui::map::MapOverlaySnapshot overlay;
    append_track_overlay(overlay);
    assert(overlay.item_count == 4 && (overlay.items[2].path_flags & 2));
    unsigned drawn = 0;
    ui::support::visitOverlayPathSegments(
        overlay,
        [](const ui::map::MapGeoPoint& p, lv_point_t& out)
        {out={int(p.lon*1000),int(p.lat*1000)};return true; },
        [&](const lv_point_t&, const lv_point_t&, ui::map::MapOverlayStyle)
        { ++drawn; });
    assert(drawn == 2);
    const auto gx = document("<k:LineString><k:coordinates>8,8,1000 9,9,2000</k:coordinates></k:LineString><g:Track><k:altitudeMode>absolute</k:altitudeMode><k:when>2024-02-29T10:00:00+08:00</k:when><k:when/><k:when>2024-02-29T10:02:00Z</k:when><g:coord>0 0 0</g:coord><g:coord/><g:coord>1 0 100</g:coord></g:Track>");
    assert(load(gx, points, samples, metrics));
    assert(points.size() == 2 && points[1].break_before && metrics.ascent_m == 0 && samples.back().distance_m == 0);
    assert(points[0].lon == 0 && points[1].lon == 1); // timed gx wins, never fake joins missing sample
    assert(load(document("<k:LineString><k:coordinates>0,0,0 .001,0 .002,0,100</k:coordinates><k:altitudeMode>absolute</k:altitudeMode></k:LineString>"), points, samples, metrics));
    assert(metrics.ascent_m == 0 && samples[2].break_before && !points[2].break_before);
    for (const auto& bad : {
             document("<g:Track><k:when>2023-02-29T00:00:00Z</k:when><g:coord>1 2 3</g:coord></g:Track>"),
             document("<g:Track><k:when>2024-01-01T00:00:00Z</k:when><g:coord/><g:coord>1 2 3</g:coord></g:Track>"),
             document("<k:LineString><k:coordinates>1,2,3x</k:coordinates></k:LineString>"),
             document("<k:Point><k:coordinates>1,2,3</k:coordinates></k:Point>"),
             segmented.substr(0, segmented.size() - 5)})
    {
        assert(!load(bad, points, samples, metrics));
        assert(points.empty() && samples.empty() && !metrics.altitude_count);
    }
    for (const auto mode : {"", "<k:altitudeMode>clampToGround</k:altitudeMode>", "<k:altitudeMode>relativeToGround</k:altitudeMode>", "<g:altitudeMode>relativeToSeaFloor</g:altitudeMode>"})
    {
        assert(load(document(std::string("<k:LineString><k:coordinates>0,0,10 1,0,20</k:coordinates>") + mode + "</k:LineString>"), points, samples, metrics));
        assert(!metrics.altitude_count && !points[0].has_altitude && !samples[1].has_altitude);
    }
    std::string long_route = "<k:LineString><k:altitudeMode>absolute</k:altitudeMode><k:coordinates>";
    for (unsigned i = 0; i < 100000; ++i) long_route += "0," + std::to_string(i / 100000.0) + "," + std::to_string(i % 2) + " ";
    long_route += "</k:coordinates></k:LineString>";
    assert(load(document(long_route), points, samples, metrics));
    assert(points.size() == 96 && samples.size() == 96 && metrics.altitude_count == 100000 && metrics.ascent_m == 50000 && metrics.descent_m == 49999);
    assert(points.front().lat == 0 && std::abs(points.back().lat - 0.99999) < 1e-9);
    assert(samples.back().distance_m > 111190 && samples.back().distance_m < 111200);
    assert(image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && !image_fake::internal_allocations);
    image_fake::allocation_fail = true;
    assert(!load(segmented, points, samples, metrics));
    image_fake::allocation_fail = false;
    assert(points.empty() && samples.empty());
    s_track_points.clear();
    assert(!image_fake::live && !kml_reader_fake::open_handles);
    struct LegacyItem
    {
        ui::map::MapOverlayKind kind;
        ui::map::MapOverlayStyle style;
        ui::map::MapGeoPoint point;
        ui::FixedText<32> label;
        ui::FixedText<64> detail;
        uint32_t id;
        uint8_t icon;
        bool selected, visible;
    };
    static_assert(sizeof(LegacyItem) == sizeof(ui::map::MapOverlayItem), "Path flags must not increase snapshot RAM");
}
