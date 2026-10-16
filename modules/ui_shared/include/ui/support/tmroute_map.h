#pragma once
#include "ui/support/tmroute_file.h"
#include "ui_map_runtime/navigation/saved_route.h"
#include <array>
#include <cmath>

namespace ui::support
{
// UI-thread owner. Indexed reads run in short poll slices; draw callbacks only
// consume a completed, clipped screen snapshot. No filesystem I/O during draw.
class TmRouteMap
{
  public:
    using Projection = bool (*)(void*, tmap::GeoPoint, tmap::ScreenPoint&);
    explicit TmRouteMap() : route_(memory_) {}
    ~TmRouteMap()
    {
        if (object_ && lv_obj_is_valid(object_))
        {
            lv_obj_remove_event_cb_with_user_data(object_, event, this);
            lv_obj_invalidate(object_);
        }
    }
    bool begin(const char* path)
    {
        route_.close();
        published_ = fill_ = 0;
        matched_ = opening_started_ = false;
        viewing_ = false;
        view_dirty_ = have_view_;
        error_ = file_.begin(path);
        opening_ = error_ == tmap::Result::More ? 1 : 0;
        if (object_) lv_obj_invalidate(object_);
        return error_ == tmap::Result::More;
    }
    void attach(lv_obj_t* object)
    {
        if (object_ == object) return;
        if (object_ && lv_obj_is_valid(object_))
        {
            lv_obj_remove_event_cb_with_user_data(object_, event, this);
            lv_obj_invalidate(object_);
        }
        object_ = object;
        if (object_)
        {
            lv_obj_add_event_cb(object_, event, LV_EVENT_ALL, this);
            lv_obj_invalidate(object_);
        }
    }
    tmap::Result error() const { return error_; }
    bool ready() const { return route_.opened(); }
    bool dense() const { return dense_; }
    bool loadingView() const { return viewing_ || view_dirty_; }
    tmap::GeoPoint destination() const { return route_.destination(); }
    double lengthM() const { return route_.lengthM(); }
    navigation::Guidance guidance(uint32_t now) const { return route_.guidance(now); }
    size_t screenSegments() const { return published_; }
    size_t readerBytes() const { return route_.reservedBytes(); }
    void storageLost()
    {
        route_.close();
        file_.close();
        opening_ = 0;
        viewing_ = false;
        published_ = 0;
        error_ = tmap::Result::IoError;
        if (object_) lv_obj_invalidate(object_);
    }
    void setViewport(const tmap::MapViewport& viewport, Projection project, void* context, int32_t padding_e7 = 0)
    {
        if (have_view_ && viewport.center.latitude == viewport_.center.latitude && viewport.center.longitude == viewport_.center.longitude &&
            viewport.zoom == viewport_.zoom && viewport.width == viewport_.width && viewport.height == viewport_.height &&
            padding_e7 == padding_e7_ && project == project_ && context == projection_context_) return;
        viewport_ = viewport;
        project_ = project;
        projection_context_ = context;
        padding_e7_ = padding_e7;
        have_view_ = true;
        view_dirty_ = true;
        published_ = 0;
        if (object_) lv_obj_invalidate(object_);
    }
    bool poll(bool fix_valid, tmap::GeoPoint fix, uint32_t now, uint32_t age,
              bool following, uint32_t (*clock)(), size_t operations = 64, uint32_t milliseconds = 4)
    {
        bool changed = false;
        const auto start = clock ? clock() : now;
        for (size_t step = 0; step < operations; ++step)
        {
            auto result = tmap::Result::Ok;
            if (opening_ == 1)
            {
                if (!opening_started_)
                {
                    opening_started_ = true;
                    opened_at_ = now;
                }
                result = file_.openStep();
                // LVGL open callbacks cannot distinguish missing files from
                // transient bus admission failure. Retry briefly, then expose it.
                if ((result == tmap::Result::Missing || result == tmap::Result::IoError) &&
                    file_.waitingForOpen() && now - opened_at_ < 1500) break;
                if (result == tmap::Result::Ok)
                {
                    result = route_.begin(file_);
                    opening_ = 2;
                }
            }
            else if (opening_ == 2)
            {
                result = route_.openStep(1);
                if (result == tmap::Result::Ok)
                {
                    opening_ = 0;
                    error_ = result;
                    changed = true;
                }
            }
            else if (!ready()) break;
            else if (view_dirty_ && have_view_)
            {
                tmap::Bounds bounds;
                result = tmap::routeArchiveViewport(viewport_, bounds);
                if (result == tmap::Result::Ok)
                {
                    if (padding_e7_ > 0)
                    {
                        bounds.south = static_cast<int32_t>(std::max(int64_t(-900000000), int64_t(bounds.south) - padding_e7_));
                        bounds.north = static_cast<int32_t>(std::min(int64_t(900000000), int64_t(bounds.north) + padding_e7_));
                        if (bounds.west != -1800000000 || bounds.east != 1800000000)
                        {
                            bounds.west = wrap(int64_t(bounds.west) - padding_e7_);
                            bounds.east = wrap(int64_t(bounds.east) + padding_e7_);
                        }
                    }
                    result = route_.beginView(bounds);
                }
                fill_ = published_ = 0;
                dense_ = view_dirty_ = false;
                viewing_ = result == tmap::Result::Ok;
            }
            else if (viewing_)
            {
                result = route_.viewStep(1, segment, this);
                if (result == tmap::Result::BudgetExceeded)
                {
                    // A dense view is a bounded-rendering limit, not corruption.
                    dense_ = true;
                    result = tmap::Result::Ok;
                }
                if (result == tmap::Result::Ok)
                {
                    viewing_ = false;
                    published_ = dense_ ? 0 : fill_;
                    if (object_) lv_obj_invalidate(object_);
                    changed = true;
                }
            }
            else if (route_.matching())
            {
                result = route_.fixStep(1);
                if (result == tmap::Result::Ok) changed = true;
            }
            else if (following && (!matched_ || now - last_match_ >= 500))
            {
                last_match_ = now;
                matched_ = true;
                result = route_.beginFix(fix, now, fix_valid ? age : 6000);
                if (result == tmap::Result::Invalid)
                {
                    changed = true;
                    break;
                } // Visible NoFix, not a storage error.
            }
            else break;
            if (result == tmap::Result::Busy) break;
            if (result != tmap::Result::Ok && result != tmap::Result::More)
            {
                platform::ui::navigation::trace("[Navigation][reader] event=failed opening=%u viewing=%u result=%s\n",
                                                unsigned(opening_), unsigned(viewing_), tmap::resultName(result));
                error_ = result;
                opening_ = 0;
                published_ = 0;
                route_.close();
                file_.close();
                if (object_) lv_obj_invalidate(object_);
                return true;
            }
            if (clock && clock() - start >= milliseconds) break;
        }
        return changed;
    }

  private:
    struct Segment
    {
        int16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    };
    static int32_t wrap(int64_t value)
    {
        if (value < -1800000000) value += 3600000000LL;
        if (value > 1800000000) value -= 3600000000LL;
        return static_cast<int32_t>(value);
    }
    static bool segment(void* context, const tmap::RouteArchiveSegment& segment)
    {
        auto& self = *static_cast<TmRouteMap*>(context);
        if (self.dense_) return true;
        tmap::ScreenPoint first{}, last{};
        const bool projected = self.project_ ? self.project_(self.projection_context_, segment.first.point, first) && self.project_(self.projection_context_, segment.last.point, last) : tmap::project(self.viewport_, segment.first.point, first) && tmap::project(self.viewport_, segment.last.point, last);
        if (!projected || !tmap::clipSegment(self.viewport_.width, self.viewport_.height, first, last)) return true;
        if (self.fill_ == self.segments_.size())
        {
            self.dense_ = true;
            return true;
        }
        auto& output = self.segments_[self.fill_++];
        output.x1 = static_cast<int16_t>(std::lround(first.x));
        output.y1 = static_cast<int16_t>(std::lround(first.y));
        output.x2 = static_cast<int16_t>(std::lround(last.x));
        output.y2 = static_cast<int16_t>(std::lround(last.y));
        return true;
    }
    static void event(lv_event_t* event)
    {
        auto& self = *static_cast<TmRouteMap*>(lv_event_get_user_data(event));
        if (lv_event_get_code(event) == LV_EVENT_DELETE)
        {
            self.object_ = nullptr;
            return;
        }
        if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN || !self.published_) return;
        auto* layer = lv_event_get_layer(event);
        if (!layer || !self.object_) return;
        lv_area_t origin;
        lv_obj_get_coords(self.object_, &origin);
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_hex(0xD15B17);
        line.width = 3;
        line.opa = LV_OPA_COVER;
        for (size_t i = 0; i < self.published_; ++i)
        {
            const auto& segment = self.segments_[i];
            line.p1 = {static_cast<lv_value_precise_t>(segment.x1 + origin.x1), static_cast<lv_value_precise_t>(segment.y1 + origin.y1)};
            line.p2 = {static_cast<lv_value_precise_t>(segment.x2 + origin.x1), static_cast<lv_value_precise_t>(segment.y2 + origin.y1)};
            lv_draw_line(layer, &line);
        }
    }
    RouteUiMemory memory_;
    LvglRouteFile file_;
    navigation::SavedRoute route_;
    std::array<Segment, 128> segments_{};
    tmap::MapViewport viewport_{};
    Projection project_ = nullptr;
    void* projection_context_ = nullptr;
    lv_obj_t* object_ = nullptr;
    size_t fill_ = 0, published_ = 0;
    uint32_t last_match_ = 0, opened_at_ = 0;
    int32_t padding_e7_ = 0;
    uint8_t opening_ = 0;
    bool have_view_ = false, view_dirty_ = false, viewing_ = false, dense_ = false, matched_ = false, opening_started_ = false;
    tmap::Result error_ = tmap::Result::More;
};
} // namespace ui::support
