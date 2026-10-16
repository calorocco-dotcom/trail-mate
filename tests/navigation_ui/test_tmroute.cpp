#include "ui/support/route_image_memory.h"
#include "ui/support/tmroute_map.h"
#include "ui/support/tmroute_preview.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static std::vector<uint8_t> source, corrupt;
static unsigned open_files = 0, calls = 0, busy_every = 0;
static uint64_t read_bytes = 0;
struct Open
{
    const std::vector<uint8_t>* bytes;
    uint32_t position = 0;
};
static void* open_file(lv_fs_drv_t*, const char* path, lv_fs_mode_t)
{
    if (std::strstr(path, "missing")) return nullptr;
    ++open_files;
    return new Open{std::strstr(path, "corrupt") ? &corrupt : &source};
}
static lv_fs_res_t close_file(lv_fs_drv_t*, void* context)
{
    assert(open_files);
    --open_files;
    delete static_cast<Open*>(context);
    return LV_FS_RES_OK;
}
static lv_fs_res_t read_file(lv_fs_drv_t*, void* context, void* output, uint32_t count, uint32_t* read)
{
    auto& file = *static_cast<Open*>(context);
    assert(file.position <= file.bytes->size());
    *read = std::min(count, static_cast<uint32_t>(file.bytes->size() - file.position));
    const bool busy = busy_every && ++calls % busy_every == 0;
    if (busy) *read /= 2;
    std::memcpy(output, file.bytes->data() + file.position, *read);
    file.position += *read;
    read_bytes += *read;
    return busy ? LV_FS_RES_BUSY : LV_FS_RES_OK;
}
static lv_fs_res_t seek_file(lv_fs_drv_t*, void* context, uint32_t position, lv_fs_whence_t whence)
{
    auto& file = *static_cast<Open*>(context);
    if (whence == LV_FS_SEEK_CUR) position += file.position;
    if (whence == LV_FS_SEEK_END) position = static_cast<uint32_t>(file.bytes->size()) - position;
    if (position > file.bytes->size()) return LV_FS_RES_INV_PARAM;
    file.position = position;
    return LV_FS_RES_OK;
}
static lv_fs_res_t tell_file(lv_fs_drv_t*, void* context, uint32_t* position)
{
    *position = static_cast<Open*>(context)->position;
    return LV_FS_RES_OK;
}
struct Point
{
    double lat = 0, lon = 0;
    bool break_before = true;
};
// Full geometry below belongs only to the host oracle. Production TmRouteMap
// retains its fixed screen snapshot and an indexed reader throughout replay.
static int london()
{
    ui::support::RouteUiMemory memory;
    ui::support::LvglRouteFile file;
    assert(file.begin("/london.tmroute") == tmap::Result::More);
    auto result = tmap::Result::More;
    while (navigation::SavedRoute::pending(result)) result = file.openStep();
    assert(result == tmap::Result::Ok);
    navigation::SavedRoute oracle(memory);
    result = oracle.begin(file);
    while (navigation::SavedRoute::pending(result)) result = oracle.openStep(1);
    assert(result == tmap::Result::Ok);
    std::vector<tmap::RouteArchiveSegment> segments;
    assert(oracle.beginView({-1800000000, -900000000, 1800000000, 900000000}) == tmap::Result::Ok);
    result = tmap::Result::More;
    while (navigation::SavedRoute::pending(result))
        result = oracle.viewStep(
            1, [](void* data, const tmap::RouteArchiveSegment& value)
            { static_cast<std::vector<tmap::RouteArchiveSegment>*>(data)->push_back(value); return true; },
            &segments);
    assert(result == tmap::Result::Ok && segments.size() > 1000);
    std::sort(segments.begin(), segments.end(), [](const auto& a, const auto& b)
              { return a.last.distance_cm < b.last.distance_cm; });
    const auto first = segments.front().first.point;
    const auto last = segments.back().last.point;
    const auto length = oracle.lengthM();
    assert(length > 20000 && length < 22000);
    oracle.close();
    file.close();
    auto* display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, static_cast<uint32_t>(pixels.size() * 2), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* value, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(value); });
    auto* object = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(object, 0, 0);
    lv_obj_set_size(object, 320, 240);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    struct Reference
    {
        const std::vector<tmap::RouteArchiveSegment>* segments;
        tmap::MapViewport viewport{};
        bool enabled = false;
    } reference{&segments};
    lv_obj_add_event_cb(
        object, [](lv_event_t* event)
        {
        const auto& ref = *static_cast<Reference*>(lv_event_get_user_data(event));
        if (!ref.enabled) return;
        auto* layer = lv_event_get_layer(event);
        lv_area_t origin;
        lv_obj_get_coords(lv_event_get_target_obj(event), &origin);
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_hex(0xD15B17);
        line.width = 3;
        line.opa = LV_OPA_COVER;
        for (const auto& segment : *ref.segments)
        {
            tmap::ScreenPoint first{}, last{};
            if (!tmap::project(ref.viewport, segment.first.point, first) || !tmap::project(ref.viewport, segment.last.point, last) ||
                !tmap::clipSegment(ref.viewport.width, ref.viewport.height, first, last)) continue;
            line.p1 = {static_cast<lv_value_precise_t>(std::lround(first.x) + origin.x1), static_cast<lv_value_precise_t>(std::lround(first.y) + origin.y1)};
            line.p2 = {static_cast<lv_value_precise_t>(std::lround(last.x) + origin.x1), static_cast<lv_value_precise_t>(std::lround(last.y) + origin.y1)};
            lv_draw_line(layer, &line);
        } },
        LV_EVENT_DRAW_MAIN, &reference);
    busy_every = 7;
    auto* route = ui::support::RouteUiMemory::create<ui::support::TmRouteMap>();
    assert(route && route->begin("/london.tmroute"));
    route->attach(object);
    uint32_t now = 1000;
    auto settle = [&](tmap::GeoPoint point, bool following, uint32_t age = 0)
    {
        for (unsigned i = 0; i < 1000; ++i)
        {
            route->poll(true, point, now, age, following, nullptr, 64);
            assert(route->error() == tmap::Result::Ok || route->error() == tmap::Result::More);
            const auto guidance = route->guidance(now);
            if (route->ready() && !route->loadingView() &&
                (!following || guidance.observed_at == now || (age > 5000 && guidance.status == navigation::GuidanceStatus::NoFix))) return;
        }
        std::cerr << "settle now=" << now << " age=" << age << " following=" << following
                  << " ready=" << route->ready() << " loading=" << route->loadingView()
                  << " observed=" << route->guidance(now).observed_at << " status=" << int(route->guidance(now).status) << '\n';
        assert(false && "native UI replay did not settle within bounded host steps");
    };
    settle(first, true);
    const auto reader_payload = route->readerBytes();
    const auto view_start = read_bytes;
    unsigned dense_views = 0;
    size_t maximum_segments = 0;
    uint64_t maximum_view_read = 0;
    unsigned pixel_comparisons = 0;
    for (const auto zoom : {12, 14, 16, 18})
        for (const auto index : {size_t(0), segments.size() / 2, segments.size() - 1})
        {
            tmap::MapViewport viewport{segments[index].last.point, static_cast<uint8_t>(zoom), 320, 240};
            const auto before = read_bytes;
            route->setViewport(viewport, nullptr, nullptr);
            assert(route->screenSegments() == 0);
            settle(first, false);
            maximum_view_read = std::max(maximum_view_read, read_bytes - before);
            maximum_segments = std::max(maximum_segments, route->screenSegments());
            dense_views += route->dense();
            if (zoom >= 16) assert(!route->dense());
            if (!route->dense())
            {
                const auto reads_before_draw = read_bytes;
                lv_refr_now(display);
                const auto actual_pixels = pixels;
                route->attach(nullptr);
                reference.viewport = viewport;
                reference.enabled = true;
                lv_obj_invalidate(object);
                lv_refr_now(display);
                assert(actual_pixels == pixels); // Indexed view must match full-geometry host oracle exactly.
                assert(read_bytes == reads_before_draw);
                reference.enabled = false;
                route->attach(object);
                ++pixel_comparisons;
            }
        }
    const auto view_read_bytes = read_bytes - view_start;
    now += 1000;
    settle(first, true, 6000);
    assert(route->guidance(now).status == navigation::GuidanceStatus::NoFix);
    auto outside = first;
    outside.latitude += 20000; // ~222 m off this route, independent of zoom.
    for (unsigned i = 0; i < 7; ++i)
    {
        now += 1000;
        settle(outside, true);
    }
    assert(route->guidance(now).status == navigation::GuidanceStatus::OffRoute);
    unsigned matched = 0, ambiguous = 0, turns = 0;
    const auto match_start = read_bytes;
    for (const auto& segment : segments)
    {
        now += 1000;
        settle(segment.last.point, true);
        const auto guide = route->guidance(now);
        if (guide.status == navigation::GuidanceStatus::Ambiguous)
        {
            ++ambiguous;
            continue;
        }
        assert(guide.status == navigation::GuidanceStatus::Following || guide.status == navigation::GuidanceStatus::Arrived);
        assert(std::fabs(guide.progress_m - segment.last.distance_cm / 100.0) < 25);
        assert(guide.remaining_m >= 0 && guide.remaining_m <= length);
        ++matched;
        turns += guide.turn != navigation::Turn::Continue;
        assert(route->readerBytes() == reader_payload);
    }
    assert(matched > 1000 && turns > 0);
    for (unsigned i = 0; i < 7; ++i)
    {
        now += 1000;
        settle(last, true);
    }
    assert(route->guidance(now).status == navigation::GuidanceStatus::Arrived);
    const auto matching_read_bytes = read_bytes - match_start;
    route->storageLost();
    assert(route->readerBytes() == 0 && open_files == 0);
    ui::support::RouteUiMemory::destroy(route);
    assert(!route);
    route = ui::support::RouteUiMemory::create<ui::support::TmRouteMap>();
    assert(route && route->begin("/london.tmroute")); // Cold reopen after all route owners were destroyed.
    settle(first, true);
    assert(route->guidance(now).status != navigation::GuidanceStatus::Arrived);
    ui::support::RouteUiMemory::destroy(route);
    assert(open_files == 0);
    lv_obj_delete(object);
    lv_display_delete(display);
    std::cout << "{\"length_m\":" << length << ",\"fixes\":" << segments.size()
              << ",\"matched\":" << matched << ",\"ambiguous\":" << ambiguous << ",\"turn_hints\":" << turns
              << ",\"ui_owner_bytes\":" << sizeof(ui::support::TmRouteMap) << ",\"reader_payload_bytes\":" << reader_payload
              << ",\"maximum_screen_segments\":" << maximum_segments << ",\"dense_views\":" << dense_views
              << ",\"view_read_bytes\":" << view_read_bytes << ",\"maximum_view_read_bytes\":" << maximum_view_read
              << ",\"matching_read_bytes\":" << matching_read_bytes << ",\"pixel_comparisons\":" << pixel_comparisons
              << ",\"busy_every\":7,\"live_files_after_close\":0}\n";
    return 0;
}
int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    source.assign(std::istreambuf_iterator<char>(input), {});
    assert(source.size() > 192);
    corrupt = source;
    corrupt[0] ^= 1;
    lv_init();
    lv_fs_drv_t driver;
    lv_fs_drv_init(&driver);
    driver.letter = 'A';
    driver.open_cb = open_file;
    driver.close_cb = close_file;
    driver.read_cb = read_file;
    driver.seek_cb = seek_file;
    driver.tell_cb = tell_file;
    lv_fs_drv_register(&driver);
    if (argc == 3)
    {
        assert(std::strcmp(argv[2], "--london") == 0);
        return london();
    }
    ui::support::RouteImageVector<Point, 96> points;
    double length = 0;
    assert(ui::support::projectTmRoutePreview("/fixture.tmroute", points, length) == tmap::Result::Ok);
    assert(points.size() >= 2 && points.front().break_before && !points.back().break_before);
    assert(points.front().lat == 0 && points.front().lon == 0 && points.back().lon == 0.002);
    assert(length > 300 && open_files == 0);
    assert(ui::support::projectTmRoutePreview("/corrupt.tmroute", points, length) != tmap::Result::Ok);
    assert(points.empty() && length == 0 && open_files == 0);
    busy_every = 1;
    assert(ui::support::projectTmRoutePreview("/fixture.tmroute", points, length) == tmap::Result::Busy);
    assert(points.empty() && open_files == 0);
    busy_every = 7;
    auto* display = lv_display_create(320, 240);
    std::vector<uint16_t> pixels(320 * 240);
    lv_display_set_buffers(display, pixels.data(), nullptr, static_cast<uint32_t>(pixels.size() * 2), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* value, const lv_area_t*, uint8_t*)
                            { lv_display_flush_ready(value); });
    auto* object = lv_obj_create(lv_screen_active());
    lv_obj_set_size(object, 320, 240);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    auto* route = ui::support::RouteUiMemory::create<ui::support::TmRouteMap>();
    assert(route && route->begin("/fixture.tmroute"));
    route->attach(object);
    tmap::MapViewport viewport{{0, 10000}, 16, 320, 240};
    route->setViewport(viewport, nullptr, nullptr);
    for (unsigned i = 0; i < 1000 && (!route->ready() || !route->screenSegments() ||
                                      route->guidance(1000).status != navigation::GuidanceStatus::Following);
         ++i)
        route->poll(true, {0, 0}, 1000, 0, true, nullptr, 64);
    assert(route->ready() && route->screenSegments() >= 3 && !route->dense());
    assert(route->guidance(1000).turn == navigation::Turn::Left);
    const auto before_draw = read_bytes;
    lv_refr_now(display);
    assert(read_bytes == before_draw); // Production draw callback must not access SD.
    viewport.center = {1000000, 1000000};
    route->setViewport(viewport, nullptr, nullptr);
    assert(route->screenSegments() == 0); // Never draw stale-camera segments while panning.
    for (unsigned i = 0; i < 100; ++i) route->poll(true, {0, 0}, 2000, 0, false, nullptr, 64);
    assert(!route->loadingView() && route->screenSegments() == 0);
    std::cout << "UI native route object=" << sizeof(ui::support::TmRouteMap)
              << " reader_payload=" << route->readerBytes() << " bytes; preview and indexed view PASS\n";
    route->storageLost();
    assert(route->readerBytes() == 0 && route->screenSegments() == 0 && open_files == 0);
    ui::support::RouteUiMemory::destroy(route);
    lv_obj_delete(object);
    lv_display_delete(display);
    assert(open_files == 0);
}
