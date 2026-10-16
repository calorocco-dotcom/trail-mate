#pragma once

#include "lvgl.h"
#include "ui/support/route_image_memory.h"

#include <cstddef>

namespace ui::widgets::route_elevation_profile
{

struct Sample
{
    double altitude_m = 0.0;
    double distance_m = 0.0;
    bool has_altitude = false;
    bool break_before = false;
};
static_assert(sizeof(Sample) <= 24, "Segment flag must fit existing sample padding");

struct Metrics
{
    std::size_t altitude_count = 0;
    double min_altitude_m = 0.0;
    double max_altitude_m = 0.0;
    double ascent_m = 0.0;
    double descent_m = 0.0;
    double distance_m = 0.0;
};

struct Config
{
    lv_coord_t height = 54;
    lv_coord_t min_width = 110;
    lv_coord_t inset = 4;
    lv_coord_t plot_top = 18;
    lv_coord_t plot_pad = 5;
    std::size_t max_points = 96;
};

struct Widget
{
    lv_obj_t* panel = nullptr;
    lv_obj_t* label = nullptr;
    lv_obj_t* line = nullptr;
    ::ui::support::RouteImageVector<lv_point_precise_t, 192> points{};
};

void reset(Widget& widget);
void create(lv_obj_t* parent, Widget& widget, const Config& config = Config{});
void set_hidden(Widget& widget, bool hidden);
bool update(Widget& widget,
            const Config& config,
            const Sample* samples,
            std::size_t sample_count,
            const Metrics& metrics,
            bool visible,
            lv_coord_t bottom_inset);

} // namespace ui::widgets::route_elevation_profile
