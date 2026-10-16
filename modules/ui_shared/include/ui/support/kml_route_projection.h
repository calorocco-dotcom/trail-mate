#pragma once
#include "ui/support/kml_route_identity.h"
#include "ui/support/route_image_kml_reader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>
namespace ui::support
{
struct KmlElevationMetrics
{
    size_t altitude_count = 0;
    double min_altitude_m = 0, max_altitude_m = 0, ascent_m = 0, descent_m = 0, previous_altitude_m = 0;
    bool has_previous_altitude = false;
};
template <class Points, class Samples, class Metrics>
class KmlRouteProjection
{
  public:
    using Point = std::remove_reference_t<decltype(std::declval<Points&>()[0])>;
    using Sample = std::remove_reference_t<decltype(std::declval<Samples&>()[0])>;
    KmlRouteProjection(Points& points, Samples& samples, Metrics& metrics, size_t budget)
        : points_(points), samples_(samples), metrics_(metrics), budget_(budget) {}
    KmlRouteIdentity source_identity() const
    {
        KmlRouteIdentity result;
        for (unsigned i = 0; i < 2; ++i)
        {
            result.counts[i] = counts_[i];
            result.hashes[i] = hashes_[i];
        }
        return result;
    }
    bool load(const char* path)
    {
        points_.clear();
        samples_.clear();
        metrics_ = {};
        track_sharing::KmlImageCallbacks callbacks{};
        callbacks.context = this;
        callbacks.coordinate = [](void* context, bool gx, double lat, double lon, double altitude, bool has_altitude, bool broken)
        { static_cast<KmlRouteProjection*>(context)->coordinate(gx, lat, lon, altitude, has_altitude, broken); };
        callbacks.geometry = [](void* context, bool gx, bool begin, track_sharing::KmlAltitudeMode mode)
        { static_cast<KmlRouteProjection*>(context)->geometry(gx, begin, mode); };
        if (!path || !budget_ || budget_ > 96 || !readRouteImageKml(path, callbacks)) return false;
        selected_ = counts_[1] ? 1 : 0;
        total_ = counts_[selected_];
        wanted_ = std::min<size_t>(budget_, total_);
        if (!wanted_) return true; // valid image/point feature, no walking path
        if (!points_.reserve(wanted_) || !samples_.reserve(wanted_)) return clear();
        second_ = true;
        if (!readRouteImageKml(path, callbacks) || failed_ || ordinal_ != total_ || second_hash_ != hashes_[selected_] || points_.size() != wanted_) return clear();
        return true;
    }

  private:
    void geometry(bool gx, bool begin, track_sharing::KmlAltitudeMode mode)
    {
        const unsigned kind = gx ? 1 : 0;
        if (!second_)
        {
            if (!begin) mix(hashes_[kind], &mode, sizeof(mode));
            return;
        }
        if (kind != selected_) return;
        if (begin)
        {
            metrics_before_ = metrics_;
            geometry_start_ = points_.size();
            return;
        }
        mix(second_hash_, &mode, sizeof(mode));
        if (mode != track_sharing::KmlAltitudeMode::Absolute)
        {
            metrics_ = metrics_before_;
            metrics_.has_previous_altitude = false;
            for (size_t i = geometry_start_; i < points_.size(); ++i) points_[i].has_altitude = false, samples_[i].has_altitude = false;
            pending_profile_break_ = true;
        }
    }
    bool clear()
    {
        points_.clear();
        samples_.clear();
        metrics_ = {};
        return false;
    }
    static void mix(uint64_t& hash, const void* bytes, size_t count)
    {
        const auto* data = static_cast<const uint8_t*>(bytes);
        for (size_t i = 0; i < count; ++i)
        {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
    }
    static double distance(const Point& a, const Point& b)
    {
        constexpr double radians = 0.017453292519943295;
        const auto dlat = (b.lat - a.lat) * radians, dlon = (b.lon - a.lon) * radians;
        const auto x = std::sin(dlat / 2), y = std::sin(dlon / 2);
        const double h = std::clamp(x * x + std::cos(a.lat * radians) * std::cos(b.lat * radians) * y * y, 0.0, 1.0);
        return 12742000.0 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
    }
    void coordinate(bool gx, double lat, double lon, double altitude, bool has_altitude, bool broken)
    {
        const unsigned kind = gx ? 1 : 0;
        uint64_t& fingerprint = second_ ? second_hash_ : hashes_[kind];
        if (second_ && kind != selected_) return;
        mix(fingerprint, &lat, sizeof(lat));
        mix(fingerprint, &lon, sizeof(lon));
        mix(fingerprint, &altitude, sizeof(altitude));
        mix(fingerprint, &has_altitude, sizeof(has_altitude));
        mix(fingerprint, &broken, sizeof(broken));
        if (!second_)
        {
            ++counts_[kind];
            return;
        }
        if (broken) have_previous_ = false, metrics_.has_previous_altitude = false, pending_geo_break_ = pending_profile_break_ = true;
        Point point{};
        point.lat = lat;
        point.lon = lon;
        point.altitude_m = altitude;
        point.has_altitude = has_altitude;
        if (have_previous_) distance_ += distance(previous_, point);
        previous_ = point;
        have_previous_ = true;
        if (!has_altitude) metrics_.has_previous_altitude = false, pending_profile_break_ = true;
        else
        {
            if (!metrics_.altitude_count) metrics_.min_altitude_m = metrics_.max_altitude_m = altitude;
            else metrics_.min_altitude_m = std::min(metrics_.min_altitude_m, altitude), metrics_.max_altitude_m = std::max(metrics_.max_altitude_m, altitude);
            if (metrics_.has_previous_altitude)
            {
                const auto delta = altitude - metrics_.previous_altitude_m;
                if (delta > 0) metrics_.ascent_m += delta;
                else metrics_.descent_m -= delta;
            }
            metrics_.previous_altitude_m = altitude;
            metrics_.has_previous_altitude = true;
            ++metrics_.altitude_count;
        }
        const size_t target = wanted_ <= 1 ? 0 : (points_.size() * (total_ - 1)) / (wanted_ - 1);
        if (ordinal_++ != target) return;
        point.break_before = pending_geo_break_;
        Sample sample{};
        sample.altitude_m = altitude;
        sample.has_altitude = has_altitude;
        sample.distance_m = distance_;
        sample.break_before = pending_profile_break_;
        if (!points_.push_back(std::move(point)) || !samples_.push_back(std::move(sample))) failed_ = true;
        pending_geo_break_ = false;
        pending_profile_break_ = !has_altitude;
    }
    Points& points_;
    Samples& samples_;
    Metrics& metrics_;
    Metrics metrics_before_{};
    size_t geometry_start_ = 0;
    size_t budget_, wanted_ = 0, total_ = 0, ordinal_ = 0;
    uint32_t counts_[2]{};
    uint64_t hashes_[2]{14695981039346656037ULL, 14695981039346656037ULL}, second_hash_ = 14695981039346656037ULL;
    Point previous_{};
    double distance_ = 0;
    unsigned selected_ = 0;
    bool second_ = false, failed_ = false, have_previous_ = false, pending_geo_break_ = true, pending_profile_break_ = true;
};
// Two validated passes, fixed output budget. Fingerprint catches changed
// geometry between passes; it is not an author signature or file hash proof.
template <class Points, class Samples, class Metrics>
bool projectKmlRoute(const char* path, Points& points, Samples& samples, Metrics& metrics, size_t budget, KmlRouteIdentity* identity = nullptr)
{
    KmlRouteProjection<Points, Samples, Metrics> projection(points, samples, metrics, budget);
    static_assert(sizeof(projection) <= 256, "Route projection owns only scalar state, never a point list");
    const bool ok = projection.load(path);
    if (identity) *identity = ok ? projection.source_identity() : KmlRouteIdentity{};
    return ok;
}
} // namespace ui::support
