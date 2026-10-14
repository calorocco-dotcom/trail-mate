#include "ui/support/disconnected_route_line.h"
#include "ui/support/route_segment_sampling.h"
#include "ui/widgets/route_elevation_profile.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
using namespace ui::widgets::route_elevation_profile;
int main()
{
    image_fake::psram = true;
    auto* parent = lv_obj_create(nullptr);
    Widget widget;
    create(parent, widget);
    assert(widget.line->callback && draw_fake::objects.size() == 4);
    Config config;
    const Sample samples[] = {{0, 0, true}, {10, 1, true}, {0, 2, false}, {20, 3, true}, {30, 4, true}};
    const Metrics metrics{4, 0, 30, 20, 0, 4};
    assert(update(widget, config, samples, 5, metrics, true, 0));
    assert(widget.points.size() == 5 && image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    draw_fake::draw(widget.line);
    assert(draw_fake::lines.size() == 2 && draw_fake::builtin_points == 0);
    assert(draw_fake::lines[0].p2.x < draw_fake::lines[1].p1.x);
    const Sample segmented[] = {{0, 0, true}, {1, 1, true}, {100, 1, true, true}, {101, 2, true}};
    assert(update(widget, config, segmented, 4, {4, 0, 101, 2, 0, 2}, true, 0));
    draw_fake::draw(widget.line);
    assert(draw_fake::lines.size() == 2); // no vertical line between two segments
    std::vector<Sample> long_route(100000);
    for (size_t i = 0; i < long_route.size(); ++i) long_route[i] = {double(i), double(i), true, i == 50000};
    config.max_points = 2;
    assert(update(widget, config, long_route.data(), long_route.size(), {long_route.size(), 0, 99999, 99998, 0, 99999}, true, 0));
    draw_fake::draw(widget.line);
    assert(draw_fake::lines.empty()); // a skipped segment boundary still breaks
    long_route[50000].break_before = false;
    long_route[50000].has_altitude = false;
    assert(update(widget, config, long_route.data(), long_route.size(), {99999, 0, 99999, 99998, 0, 99999}, true, 0));
    draw_fake::draw(widget.line);
    assert(draw_fake::lines.empty()); // skipped missing height never interpolates
    config.max_points = 96;
    for (size_t i = 0; i < long_route.size(); ++i) long_route[i].break_before = i % 4 == 0;
    assert(update(widget, config, long_route.data(), long_route.size(), {99999, 0, 99999, 99998, 0, 99999}, true, 0));
    assert(widget.points.size() <= 191);
    draw_fake::draw(widget.line);
    assert(draw_fake::lines.empty() && draw_fake::objects.size() == 4);
    image_fake::allocation_fail = true;
    assert(!update(widget, config, samples, 5, metrics, true, 0));
    assert(widget.points.empty() && image_fake::live == 0 && image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(widget.panel->flags & LV_OBJ_FLAG_HIDDEN);
    image_fake::allocation_fail = false;
    image_fake::psram = false;
    assert(update(widget, config, samples, 5, metrics, true, 0));
    assert(image_fake::last_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    reset(widget);
    assert(image_fake::live == 0);
    auto* line_object = lv_line_create(parent);
    line_object->x = 10;
    line_object->y = 20;
    line_object->scroll_x = 2;
    line_object->scroll_y = 3;
    const lv_point_precise_t points[] = {{1, 2}, {3, 4}, ui::support::routeLineBreak(), {5, 6}, {7, 8}};
    lv_event_t event{line_object, &draw_fake::layer, nullptr};
    draw_fake::lines.clear();
    ui::support::drawDisconnectedRouteLine(&event, points, 5);
    assert(draw_fake::lines.size() == 2);
    assert(draw_fake::lines[0].p1.x == 9 && draw_fake::lines[0].p1.y == 19);
    assert(draw_fake::lines[1].p1.x == 13 && draw_fake::lines[1].p1.y == 23);
    assert(draw_fake::lines[0].p2.x == 11 && draw_fake::lines[1].p2.x == 15);
    assert(draw_fake::builtin_points == 0);
}
