#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
namespace track_sharing
{
struct RouteMatchPoint
{
    double lat = 0, lon = 0;
};
inline constexpr double routeEarthRadius = 6371000.0;
inline double routeRadians(double value) { return value * 0.017453292519943295; }
inline double routeAngularDistance(RouteMatchPoint a, RouteMatchPoint b)
{
    const auto x = std::sin(routeRadians(b.lat - a.lat) / 2), y = std::sin(routeRadians(b.lon - a.lon) / 2);
    const double h = std::clamp(x * x + std::cos(routeRadians(a.lat)) * std::cos(routeRadians(b.lat)) * y * y, 0.0, 1.0);
    return 2 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
}
inline double routeBearing(RouteMatchPoint a, RouteMatchPoint b)
{
    const auto delta = routeRadians(std::remainder(b.lon - a.lon, 360.0));
    const auto la = routeRadians(a.lat), lb = routeRadians(b.lat);
    return std::atan2(std::sin(delta) * std::cos(lb), std::cos(la) * std::sin(lb) - std::sin(la) * std::cos(lb) * std::cos(delta));
}
struct RouteMatchResult
{
    double distance_m = std::numeric_limits<double>::infinity();
    double progress_m = 0, total_m = 0, endpoint_distance_m = 0;
    bool valid = false, ambiguous = false;
};
// One preceding raw point, never a retained path. Minor great-circle arcs;
// an antipodal pair cannot define a unique arc and is explicitly rejected.
class RouteMatchAccumulator
{
  public:
    explicit RouteMatchAccumulator(RouteMatchPoint fix = {}) : fix_(fix) {}
    bool point(RouteMatchPoint current, bool broken)
    {
        if (!std::isfinite(current.lat) || !std::isfinite(current.lon) || std::abs(current.lat) > 90 || std::abs(current.lon) > 180 ||
            !std::isfinite(fix_.lat) || !std::isfinite(fix_.lon) || std::abs(fix_.lat) > 90 || std::abs(fix_.lon) > 180) return false;
        const double distance = routeAngularDistance(fix_, current) * routeEarthRadius;
        if (have_previous_ && !broken)
        {
            const double length = routeAngularDistance(previous_, current);
            if (length > 3.141592653589793 - 1e-8) return false;
            if (length > 1e-12)
            {
                const double from_start = routeAngularDistance(previous_, fix_);
                const double heading = routeBearing(previous_, fix_) - routeBearing(previous_, current);
                const double along = std::atan2(std::sin(from_start) * std::cos(heading), std::cos(from_start));
                if (along >= 0 && along <= length)
                {
                    const double cross = std::abs(std::asin(std::clamp(std::sin(from_start) * std::sin(heading), -1.0, 1.0)));
                    consider(cross * routeEarthRadius, result_.total_m + along * routeEarthRadius);
                }
            }
            result_.total_m += length * routeEarthRadius;
        }
        consider(distance, result_.total_m);
        result_.endpoint_distance_m = distance;
        previous_ = current;
        have_previous_ = true;
        result_.valid = true;
        return true;
    }
    const RouteMatchResult& result() const { return result_; }

  private:
    void consider(double distance, double progress)
    {
        if (distance + 0.01 < result_.distance_m)
        {
            result_.distance_m = distance;
            result_.progress_m = progress;
            result_.ambiguous = false;
        }
        else if (std::abs(distance - result_.distance_m) <= 0.01 && std::abs(progress - result_.progress_m) > 20) result_.ambiguous = true;
    }
    RouteMatchPoint fix_, previous_{};
    RouteMatchResult result_{};
    bool have_previous_ = false;
};
static_assert(sizeof(RouteMatchAccumulator) <= 96, "Matcher retains scalar state and one point");
} // namespace track_sharing
