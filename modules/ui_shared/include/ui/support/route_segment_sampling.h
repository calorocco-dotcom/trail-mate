#pragma once
#include <algorithm>
#include <cstddef>

namespace ui::support
{
// Two linear passes; no ordinal table or allocations. Skipped source samples
// still contribute break/missing markers so decimation cannot bridge a gap.
template <class Point, class Valid, class Emit>
size_t sampleRouteSegments(const Point* points, size_t count, size_t budget, Valid&& valid, Emit&& emit)
{
    if (!points || !count || !budget) return 0;
    size_t total = 0;
    for (size_t i = 0; i < count; ++i)
        if (valid(points[i])) ++total;
    const size_t wanted = std::min(total, budget);
    if (!wanted) return 0;
    size_t ordinal = 0, emitted = 0, target = 0;
    bool broken = true;
    for (size_t i = 0; i < count && emitted < wanted; ++i)
    {
        const auto& point = points[i];
        if (point.break_before) broken = true;
        if (!valid(point))
        {
            broken = true;
            continue;
        }
        if (ordinal++ != target) continue;
        if (!emit(point, emitted != 0 && !broken)) break;
        broken = false;
        ++emitted;
        target = wanted <= 1 ? 0 : (emitted * (total - 1)) / (wanted - 1);
    }
    return emitted;
}
} // namespace ui::support
