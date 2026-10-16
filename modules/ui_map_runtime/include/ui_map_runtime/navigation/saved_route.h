#pragma once
#include "tmap/memory_object.h"
#include "tmap/route_archive.h"
#include "track_sharing/route_match.h"
#include <algorithm>
#include <cmath>

namespace navigation
{
enum class GuidanceStatus : uint8_t
{
    Idle,
    Locating,
    Following,
    Ambiguous,
    OffRoute,
    Arrived,
    NoFix,
    StorageError
};
enum class Turn : uint8_t
{
    Continue,
    Left,
    Right,
    UTurn
};
struct Guidance
{
    GuidanceStatus status = GuidanceStatus::Idle;
    Turn turn = Turn::Continue;
    double progress_m = 0, remaining_m = 0, deviation_m = 0, turn_m = 0;
    uint32_t observed_at = 0;
};

// One shared archive page and two small cursors, independent of route length.
// Matching, lookahead and display are cooperative; no RTOS, UI or map dependency.
class SavedRoute
{
  public:
    explicit SavedRoute(tmap::MemoryProvider& memory, tmap::MemoryPolicy policy = tmap::MemoryPolicy::RequireExternal)
        : workspace_(memory, policy), reader_(memory, policy), query_(memory, policy), view_(memory, policy) {}
    ~SavedRoute() { close(); }
    SavedRoute(const SavedRoute&) = delete;
    SavedRoute& operator=(const SavedRoute&) = delete;
    static bool pending(tmap::Result value) { return value == tmap::Result::More || value == tmap::Result::Busy; }
    tmap::Result begin(tmap::RandomAccessFile& file)
    {
        close();
        auto result = workspace_.create();
        if (result == tmap::Result::Ok) result = reader_.create(*workspace_.get());
        if (result == tmap::Result::Ok) result = query_.create();
        if (result == tmap::Result::Ok) result = view_.create();
        if (result != tmap::Result::Ok)
        {
            close();
            return result;
        }
        file_ = &file;
        opening_ = 1;
        return tmap::Result::More;
    }
    tmap::Result openStep(size_t budget)
    {
        if (!budget || !opening_) return opened_ ? tmap::Result::Ok : tmap::Result::Invalid;
        if (opening_ == 1)
        {
            const auto result = reader_.get()->open(*file_);
            if (pending(result)) return result;
            if (result != tmap::Result::Ok) return error(result);
            length_m_ = reader_.get()->info().length_cm / 100.0;
            if (reader_.get()->info().points < 2) return error(tmap::Result::Invalid);
            tmap::RouteArchiveQuery query;
            query.first_cm = reader_.get()->info().length_cm;
            query.max_nodes = 4096;
            query.max_segments = 4096;
            const auto begin = reader_.get()->beginQuery(query, *query_.get());
            if (begin != tmap::Result::Ok) return error(begin);
            opening_ = 2;
            return tmap::Result::More;
        }
        const auto result = reader_.get()->queryStep(*query_.get(), budget, endpoint, this);
        if (pending(result)) return result;
        if (result != tmap::Result::Ok || !endpoint_found_) return error(result == tmap::Result::Ok ? tmap::Result::Invalid : result);
        opened_ = true;
        opening_ = 0;
        return tmap::Result::Ok;
    }
    void close()
    {
        view_.reset();
        query_.reset();
        reader_.reset();
        workspace_.reset();
        file_ = nullptr;
        opened_ = endpoint_found_ = armed_ = near_ = far_ = false;
        opening_ = matching_ = 0;
        guidance_ = {};
        working_ = {};
        length_m_ = 0;
    }
    tmap::GeoPoint destination() const { return destination_; }
    double lengthM() const { return length_m_; }
    bool opened() const { return opened_; }
    bool matching() const { return matching_ != 0; }
    Guidance guidance(uint32_t now) const
    {
        auto value = guidance_;
        if (value.status != GuidanceStatus::Idle && value.status != GuidanceStatus::StorageError &&
            now - value.observed_at > 5000) value.status = GuidanceStatus::NoFix;
        return value;
    }
    size_t reservedBytes() const { return workspace_.bytes() + reader_.bytes() + query_.bytes() + view_.bytes(); }

    tmap::Result beginView(const tmap::Bounds& bounds, uint64_t max_segments = 8192)
    {
        if (!opened_) return tmap::Result::Invalid;
        tmap::RouteArchiveQuery query;
        query.viewport = bounds;
        query.max_nodes = 4096;
        query.max_segments = max_segments;
        return reader_.get()->beginQuery(query, *view_.get());
    }
    tmap::Result viewStep(size_t budget, tmap::RouteArchiveVisitor visitor, void* context)
    {
        if (!opened_) return tmap::Result::Invalid;
        return reader_.get()->queryStep(*view_.get(), budget, visitor, context);
    }
    tmap::Result beginFix(tmap::GeoPoint point, uint32_t now, uint32_t age = 0, double accuracy_m = 0)
    {
        if (!opened_) return tmap::Result::Invalid;
        if (point.latitude < -900000000 || point.latitude > 900000000 || point.longitude < -1800000000 ||
            point.longitude > 1800000000 || age > 5000 || !std::isfinite(accuracy_m) || accuracy_m < 0 || accuracy_m > 30)
        {
            matching_ = 0;
            near_ = far_ = false;
            guidance_.status = GuidanceStatus::NoFix;
            return tmap::Result::Invalid;
        }
        fix_ = {point.latitude / 1e7, point.longitude / 1e7};
        observed_at_ = now - age;
        accuracy_m_ = accuracy_m;
        best_ = {};
        best_.total_m = length_m_;
        best_.endpoint_distance_m = track_sharing::routeAngularDistance(fix_, geo(destination_)) * track_sharing::routeEarthRadius;
        tmap::RouteArchiveQuery query;
        // The box contains a 100 m circle. Missing candidates establish a lower
        // bound rather than inventing the nearest point elsewhere on the route.
        const auto latitude_delta = int64_t(100 / 111000.0 * 1e7) + 1;
        const auto scale = std::max(0.001, std::cos(track_sharing::routeRadians(fix_.lat)));
        const auto longitude_delta = int64_t(latitude_delta / scale) + 1;
        query.viewport.south = static_cast<int32_t>(std::max(int64_t(-900000000), int64_t(point.latitude) - latitude_delta));
        query.viewport.north = static_cast<int32_t>(std::min(int64_t(900000000), int64_t(point.latitude) + latitude_delta));
        query.viewport.west = wrap(int64_t(point.longitude) - longitude_delta);
        query.viewport.east = wrap(int64_t(point.longitude) + longitude_delta);
        if (std::abs(point.latitude) >= 899000000)
        {
            query.viewport.west = -1800000000;
            query.viewport.east = 1800000000;
        }
        query.max_nodes = 4096;
        query.max_segments = 8192;
        const auto result = reader_.get()->beginQuery(query, *query_.get());
        if (result != tmap::Result::Ok) return error(result);
        matching_ = 1;
        if (guidance_.status == GuidanceStatus::Idle) guidance_.status = GuidanceStatus::Locating;
        return tmap::Result::More;
    }
    tmap::Result fixStep(size_t budget)
    {
        if (!opened_ || !matching_ || !budget) return tmap::Result::Invalid;
        const auto result = reader_.get()->queryStep(*query_.get(), budget, matching_ == 1 ? match : lookahead, this);
        if (pending(result)) return result;
        if (result != tmap::Result::Ok) return error(result);
        if (matching_ == 1)
        {
            commitMatch();
            if (working_.status != GuidanceStatus::Following)
            {
                guidance_ = working_;
                matching_ = 0;
                return tmap::Result::Ok;
            }
            tmap::RouteArchiveQuery query;
            query.first_cm = static_cast<uint64_t>(working_.progress_m * 100);
            query.last_cm = query.first_cm + 20000;
            query.max_nodes = 4096;
            query.max_segments = 2048;
            const auto begin = reader_.get()->beginQuery(query, *query_.get());
            if (begin != tmap::Result::Ok) return error(begin);
            have_bearing_ = turn_found_ = false;
            matching_ = 2;
            return tmap::Result::More;
        }
        guidance_ = working_;
        matching_ = 0;
        return tmap::Result::Ok;
    }

  private:
    static int32_t wrap(int64_t value)
    {
        if (value > 1800000000) value -= 3600000000LL;
        if (value < -1800000000) value += 3600000000LL;
        return static_cast<int32_t>(value);
    }
    static track_sharing::RouteMatchPoint geo(tmap::GeoPoint point) { return {point.latitude / 1e7, point.longitude / 1e7}; }
    static bool endpoint(void* context, const tmap::RouteArchiveSegment& segment)
    {
        auto& self = *static_cast<SavedRoute*>(context);
        if (segment.last.distance_cm == self.reader_.get()->info().length_cm)
        {
            self.destination_ = segment.last.point;
            self.endpoint_found_ = true;
        }
        return true;
    }
    static bool match(void* context, const tmap::RouteArchiveSegment& segment)
    {
        auto& self = *static_cast<SavedRoute*>(context);
        track_sharing::RouteMatchAccumulator accumulator(self.fix_);
        if (!accumulator.point(geo(segment.first.point), true) || !accumulator.point(geo(segment.last.point), false)) return false;
        const auto& candidate = accumulator.result();
        const auto length = candidate.total_m;
        const auto fraction = length > 0 ? std::clamp(candidate.progress_m / length, 0.0, 1.0) : 0;
        const double progress = (segment.first.distance_cm + fraction * (segment.last.distance_cm - segment.first.distance_cm)) / 100.0;
        if (candidate.distance_m + 0.25 < self.best_.distance_m)
        {
            self.best_.distance_m = candidate.distance_m;
            self.best_.progress_m = progress;
            self.best_.ambiguous = false;
        }
        else if (std::abs(candidate.distance_m - self.best_.distance_m) <= 0.25 &&
                 std::abs(progress - self.best_.progress_m) > 20) self.best_.ambiguous = true;
        self.best_.valid = true;
        return true;
    }
    void commitMatch()
    {
        const uint32_t previous = guidance_.observed_at;
        working_ = guidance_;
        working_.observed_at = observed_at_;
        working_.turn = Turn::Continue;
        working_.turn_m = 0;
        working_.deviation_m = best_.valid ? best_.distance_m : 100;
        if (observed_at_ - previous > 6000) near_ = far_ = false;
        if (best_.ambiguous)
        {
            near_ = far_ = false;
            working_.status = GuidanceStatus::Ambiguous;
            return;
        }
        const bool distant = working_.deviation_m - accuracy_m_ > 35;
        if (distant)
        {
            near_ = false;
            if (!far_)
            {
                far_ = true;
                far_since_ = observed_at_;
            }
            working_.status = observed_at_ - far_since_ >= 5000 ? GuidanceStatus::OffRoute : GuidanceStatus::Locating;
            return;
        }
        far_ = false;
        if (!best_.valid || working_.deviation_m + accuracy_m_ > 15)
        {
            near_ = false;
            working_.status = GuidanceStatus::Locating;
            return;
        }
        working_.progress_m = best_.progress_m;
        working_.remaining_m = std::max(0.0, length_m_ - best_.progress_m);
        if (working_.remaining_m > 20) armed_ = true;
        const bool at_end = armed_ && working_.remaining_m <= 10 && best_.endpoint_distance_m + accuracy_m_ <= 10;
        if (at_end && !near_)
        {
            near_ = true;
            near_since_ = observed_at_;
        }
        if (!at_end) near_ = false;
        working_.status = near_ && observed_at_ - near_since_ >= 5000 ? GuidanceStatus::Arrived : GuidanceStatus::Following;
    }
    static bool lookahead(void* context, const tmap::RouteArchiveSegment& segment)
    {
        auto& self = *static_cast<SavedRoute*>(context);
        if (self.turn_found_ || segment.last.break_before) return true;
        const double length = (segment.last.distance_cm - segment.first.distance_cm) / 100.0;
        if (length < 3 || segment.last.distance_cm / 100.0 < self.working_.progress_m) return true;
        const auto bearing = track_sharing::routeBearing(geo(segment.first.point), geo(segment.last.point));
        if (self.have_bearing_)
        {
            const auto angle = std::remainder(bearing - self.bearing_, 6.283185307179586);
            if (std::abs(angle) >= 0.7853981633974483 && segment.first.distance_cm / 100.0 >= self.working_.progress_m)
            {
                self.working_.turn = std::abs(angle) >= 2.6179938779914944 ? Turn::UTurn : angle > 0 ? Turn::Right
                                                                                                     : Turn::Left;
                self.working_.turn_m = segment.first.distance_cm / 100.0 - self.working_.progress_m;
                self.turn_found_ = true;
            }
        }
        self.bearing_ = bearing;
        self.have_bearing_ = true;
        return true;
    }
    tmap::Result error(tmap::Result value)
    {
        matching_ = opening_ = 0;
        guidance_.status = GuidanceStatus::StorageError;
        return value;
    }
    tmap::MemoryObject<tmap::RouteArchiveWorkspace> workspace_;
    tmap::MemoryObject<tmap::RouteArchiveReader> reader_;
    tmap::MemoryObject<tmap::RouteArchiveQueryCursor> query_, view_;
    tmap::RandomAccessFile* file_ = nullptr;
    tmap::GeoPoint destination_{};
    track_sharing::RouteMatchPoint fix_{};
    track_sharing::RouteMatchResult best_{};
    Guidance guidance_{}, working_{};
    double length_m_ = 0, accuracy_m_ = 0, bearing_ = 0;
    uint32_t observed_at_ = 0, near_since_ = 0, far_since_ = 0;
    uint8_t opening_ = 0, matching_ = 0;
    bool opened_ = false, endpoint_found_ = false, armed_ = false, near_ = false, far_ = false;
    bool have_bearing_ = false, turn_found_ = false;
};
} // namespace navigation
