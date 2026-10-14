#pragma once
#include "track_sharing/route_match.h"
namespace track_sharing
{
enum class RouteNavigationStatus : uint8_t
{
    Unavailable,
    Ambiguous,
    Position,
    Arrived
};
struct RouteNavigationView
{
    double remaining_m = 0;
    unsigned percent = 0;
    RouteNavigationStatus status = RouteNavigationStatus::Unavailable;
};
// Position on the recorded path is not distance walked. No extrapolation,
// bridging of disconnected legs, or arrival inference at a loop's start.
class RouteNavigationState
{
  public:
    void reset() { *this = {}; }
    void observe(const RouteMatchResult& result, uint32_t completed, double movement)
    {
        if (observed_ && completed == last_observed_) return;
        if (observed_ && completed - last_observed_ > 6000) near_ = arrived_ = false;
        observed_ = true;
        last_observed_ = completed;
        if (!usable(result, movement) || result.ambiguous)
        {
            near_ = arrived_ = false;
            return;
        }
        const double remaining = std::max(0.0, result.total_m - result.progress_m);
        if (remaining >= 20) armed_ = true;
        if (!armed_ || !near_end(result, movement))
        {
            near_ = arrived_ = false;
            return;
        }
        if (!near_)
        {
            near_ = true;
            near_since_ = completed;
        }
        // Only independent completed scans count toward the five-second dwell.
        arrived_ = completed - near_since_ >= 5000;
    }
    RouteNavigationView project(const RouteMatchResult& result, uint32_t age, double movement) const
    {
        RouteNavigationView view;
        if (age > 5000 || !usable(result, movement)) return view;
        if (result.ambiguous)
        {
            view.status = RouteNavigationStatus::Ambiguous;
            return view;
        }
        view.remaining_m = std::max(0.0, result.total_m - result.progress_m);
        view.percent = static_cast<unsigned>(std::clamp(result.progress_m / result.total_m * 100.0, 0.0, 100.0));
        view.status = arrived_ && near_end(result, movement) ? RouteNavigationStatus::Arrived : RouteNavigationStatus::Position;
        return view;
    }

  private:
    static bool usable(const RouteMatchResult& result, double movement)
    {
        return result.valid && std::isfinite(result.total_m) && result.total_m > 0 &&
               std::isfinite(result.progress_m) && result.progress_m >= 0 && result.progress_m <= result.total_m &&
               std::isfinite(result.distance_m) && result.distance_m >= 0 &&
               std::isfinite(result.endpoint_distance_m) && result.endpoint_distance_m >= 0 &&
               std::isfinite(movement) && movement >= 0 && movement <= 3 && result.distance_m + movement <= 7;
    }
    static bool near_end(const RouteMatchResult& result, double movement)
    {
        return result.total_m - result.progress_m <= 10 && result.endpoint_distance_m + movement <= 10;
    }
    uint32_t last_observed_ = 0, near_since_ = 0;
    bool observed_ = false, armed_ = false, near_ = false, arrived_ = false;
};
static_assert(sizeof(RouteNavigationState) <= 16, "Arrival keeps only scalar state in the route owner");
} // namespace track_sharing
