#pragma once
#include "tmap/memory_object.h"
#include "ui/support/tmroute_file.h"
#include <cmath>
#include <type_traits>

namespace ui::support
{
template <class Points>
struct TmRoutePreviewWork
{
    RouteUiMemory memory;
    LvglRouteFile file;
    tmap::MemoryObject<tmap::RouteArchiveWorkspace> workspace{memory};
    tmap::MemoryObject<tmap::RouteArchiveReader> reader{memory};
    tmap::MemoryObject<tmap::RouteArchiveQueryCursor> cursor{memory};
    Points& points;
    size_t capacity;
    double spacing = 0, next = 0;
    tmap::RouteArchivePoint last{};
    bool have_last = false, broken = true, failed = false;
    explicit TmRoutePreviewWork(Points& output, size_t limit) : points(output), capacity(limit) {}
    bool append(const tmap::RouteArchivePoint& source)
    {
        using Point = typename std::remove_reference<decltype(points.front())>::type;
        Point point{};
        point.lat = source.point.latitude / 1e7;
        point.lon = source.point.longitude / 1e7;
        point.break_before = broken || source.break_before;
        if (!points.push_back(point))
        {
            failed = true;
            return false;
        }
        broken = false;
        return true;
    }
    static bool visit(void* context, const tmap::RouteArchiveSegment& segment)
    {
        auto& self = *static_cast<TmRoutePreviewWork*>(context);
        if (!self.have_last || self.last.point.latitude != segment.first.point.latitude ||
            self.last.point.longitude != segment.first.point.longitude)
        {
            self.broken = true;
            if (self.points.size() >= self.capacity - 1)
            {
                self.failed = true;
                return false;
            }
            if (!self.append(segment.first)) return false;
            self.next = segment.first.distance_cm / 100.0 + self.spacing;
        }
        if (segment.last.distance_cm / 100.0 >= self.next && self.points.size() < self.capacity - 1)
        {
            if (!self.append(segment.last)) return false;
            self.next = segment.last.distance_cm / 100.0 + self.spacing;
        }
        self.last = segment.last;
        self.have_last = true;
        return true;
    }
};
// Fixed-size overview only. Detailed navigation uses the indexed viewport path.
// Busy/errors do not publish a partial thumbnail; UI offers a retry.
template <class Points>
tmap::Result projectTmRoutePreview(const char* path, Points& points, double& length_m, size_t capacity = 96)
{
    points.clear();
    length_m = 0;
    if (capacity < 2) return tmap::Result::Invalid;
    auto* work = RouteUiMemory::create<TmRoutePreviewWork<Points>>(points, capacity);
    if (!work) return tmap::Result::BudgetExceeded;
    auto result = work->file.begin(path);
    for (unsigned i = 0; i < 8 && result == tmap::Result::More; ++i) result = work->file.openStep();
    if (result == tmap::Result::Ok) result = work->workspace.create();
    if (result == tmap::Result::Ok) result = work->reader.create(*work->workspace.get());
    if (result == tmap::Result::Ok) result = work->cursor.create();
    if (result == tmap::Result::Ok) result = work->reader.get()->open(work->file);
    if (result == tmap::Result::Ok)
    {
        length_m = work->reader.get()->info().length_cm / 100.0;
        work->spacing = length_m / (capacity - 1);
        tmap::RouteArchiveQuery query;
        query.max_nodes = 4096;
        query.max_segments = 8192;
        result = work->reader.get()->beginQuery(query, *work->cursor.get());
        if (result == tmap::Result::Ok)
        {
            do
            {
                result = work->reader.get()->queryStep(*work->cursor.get(), 32, work->visit, work);
            } while (result == tmap::Result::More);
        }
        if (result == tmap::Result::Ok && work->have_last)
        {
            const auto& last = points.back();
            if ((last.lat != work->last.point.latitude / 1e7 || last.lon != work->last.point.longitude / 1e7) &&
                !work->append(work->last)) result = tmap::Result::BudgetExceeded;
        }
        else if (result == tmap::Result::Ok) result = tmap::Result::Invalid;
    }
    if (work->failed) result = tmap::Result::BudgetExceeded;
    RouteUiMemory::destroy(work);
    if (result != tmap::Result::Ok)
    {
        points.clear();
        length_m = 0;
    }
    return result;
}
} // namespace ui::support
