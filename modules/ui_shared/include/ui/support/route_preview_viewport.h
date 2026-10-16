#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace ui::support
{
// Scalar view state only: route geometry remains in the indexed SD reader.
struct RoutePreviewViewport
{
    int zoom = -1, pan_x = 0, pan_y = 0, drag_x = 0, drag_y = 0;
    void reset() { *this = {}; }
    static int bounded(int64_t value)
    {
        constexpr int64_t limit = INT64_C(1) << 27;
        return static_cast<int>(std::clamp(value, -limit, limit));
    }
    void pan(int x, int y)
    {
        pan_x = bounded(int64_t(pan_x) + x);
        pan_y = bounded(int64_t(pan_y) + y);
    }
    void beginDrag()
    {
        drag_x = pan_x;
        drag_y = pan_y;
    }
    void drag(int x, int y)
    {
        pan_x = bounded(int64_t(drag_x) + x);
        pan_y = bounded(int64_t(drag_y) + y);
    }
    bool zoomBy(int delta)
    {
        if (zoom < 0) return false;
        const auto next = std::clamp(zoom + delta, 0, 18);
        if (next == zoom) return false;
        const auto scale = INT64_C(1) << std::abs(next - zoom);
        pan_x = bounded(next > zoom ? int64_t(pan_x) * scale : int64_t(pan_x) / scale);
        pan_y = bounded(next > zoom ? int64_t(pan_y) * scale : int64_t(pan_y) / scale);
        zoom = next;
        return true;
    }
};
} // namespace ui::support
