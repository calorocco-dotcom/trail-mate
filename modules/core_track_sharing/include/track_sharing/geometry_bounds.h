#pragma once
#include <algorithm>
#include <cstdint>
namespace track_sharing
{
struct GeometryExtent
{
    int32_t west = 0, south = 0, east = 0, north = 0;
    uint32_t points = 0;
};
// Index profile: normalize E7 longitudes, unwrap each around the first point,
// then enclose all quantized points. Safe conservative arc, not minimal arc.
class GeometryBounds
{
  public:
    bool add(int64_t longitude, int64_t latitude)
    {
        if (failed_ || longitude < -1800000000LL || longitude > 1800000000LL || latitude < -900000000LL || latitude > 900000000LL || points_ >= 100000)
        {
            failed_ = true;
            return false;
        }
        longitude = wrap(longitude);
        if (!points_)
        {
            anchor_ = static_cast<int32_t>(longitude);
            west_ = east_ = longitude;
            south_ = north_ = static_cast<int32_t>(latitude);
        }
        else
        {
            int64_t delta = longitude - anchor_;
            if (delta >= 1800000000LL) delta -= 3600000000LL;
            if (delta < -1800000000LL) delta += 3600000000LL;
            longitude = anchor_ + delta;
            west_ = std::min(west_, longitude);
            east_ = std::max(east_, longitude);
            south_ = std::min(south_, static_cast<int32_t>(latitude));
            north_ = std::max(north_, static_cast<int32_t>(latitude));
        }
        ++points_;
        return true;
    }
    bool finish(GeometryExtent& output) const
    {
        output = {};
        if (failed_ || !points_) return false;
        output = {static_cast<int32_t>(wrap(west_)), south_, static_cast<int32_t>(wrap(east_)), north_, points_};
        return true;
    }
    uint32_t pointCount() const { return points_; }

  private:
    static int64_t wrap(int64_t value)
    {
        if (value >= 1800000000LL) value -= 3600000000LL;
        if (value < -1800000000LL) value += 3600000000LL;
        return value;
    }
    int64_t west_ = 0, east_ = 0;
    int32_t anchor_ = 0, south_ = 0, north_ = 0;
    uint32_t points_ = 0;
    bool failed_ = false;
};
static_assert(sizeof(GeometryBounds) <= 40, "Geometry bounds must not retain a point list");
} // namespace track_sharing
