#include "ui/screens/tracker/tracker_item_card_view.h"
#include "ui/support/card_file_location.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

static std::string title(const std::string& xml)
{
    ui::support::KmlCardTitle parser;
    for (unsigned char byte : xml) parser.feed(byte);
    return parser.title();
}

int main()
{
    assert(title("<kml><Document><name> North &amp; South </name><Placemark><name>Start</name></Placemark></Document></kml>") == "North & South");
    assert(title("<k:kml><k:Document><k:Placemark><k:name>Point</k:name></k:Placemark><k:name><![CDATA[山地步道]]></k:name></k:Document></k:kml>") == "山地步道");
    assert(title("<kml><Document><Style><name>Style</name></Style><Placemark><name>Trail</name></Placemark></Document></kml>") == "Trail");
    assert(title("<kml><Document><name>   </name></Document></kml>").empty());
    const auto long_title = title("<kml><Document><name>" + std::string(300, 'x') + "</name></Document></kml>");
    assert(long_title.size() == 192);
    std::string chinese;
    for (int i = 0; i < 80; ++i) chinese += "山";
    assert(title("<kml><Document><name>" + chinese + "</name></Document></kml>") == chinese.substr(0, 192));
    const auto hash_file = "p-" + std::string(64, 'a') + ".kml";
    assert(ui::support::cardFileTitleFallback(hash_file, "Route") == "Route aaaaaaaa");
    assert(ui::support::cardFileTitleFallback("North Ridge.kml", "Route") == "North Ridge");
    lv_init();
    lv_fs_drv_t fs{};
    lv_fs_drv_init(&fs);
    fs.letter = 'S';
    fs.open_cb = [](lv_fs_drv_t*, const char* path, lv_fs_mode_t) -> void*
    { return std::fopen(path, "rb"); };
    fs.read_cb = [](lv_fs_drv_t*, void* file, void* data, uint32_t size, uint32_t* read)
    { *read = std::fread(data, 1, size, static_cast<FILE*>(file)); return LV_FS_RES_OK; };
    fs.close_cb = [](lv_fs_drv_t*, void* file)
    { std::fclose(static_cast<FILE*>(file)); return LV_FS_RES_OK; };
    lv_fs_drv_register(&fs);
    {
        std::ofstream file("selected.kml");
        file << "<kml><Document><name>North Ridge Loop</name><Placemark><LineString><coordinates>121,31,0 122,32,0</coordinates></LineString></Placemark></Document></kml>";
    }
    double latitude = 0, longitude = 0;
    std::string selected_title;
    assert(ui::support::readCardFileLocation("S:selected.kml", latitude, longitude, &selected_title));
    assert(selected_title == "North Ridge Loop" && latitude == 31 && longitude == 121);
    for (int width : {320, 480, 240})
    {
        const int height = width == 320 ? 240 : width == 480 ? 222
                                                             : 320;
        auto* display = lv_display_create(width, height);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        std::vector<uint16_t> pixels(width * height);
        lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 2, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*)
                                { lv_display_flush_ready(d); });
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xFFF7E9), 0);
        auto* filter = lv_label_create(lv_screen_active());
        lv_obj_set_pos(filter, 8, 45);
        lv_label_set_text(filter, "Tracker\n\nTracks\n\nRoutes\n\nNavigation");
        lv_obj_set_style_text_font(filter, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(filter, lv_color_hex(0x6B4A1E), 0);
        auto* parent = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(parent);
        lv_obj_set_pos(parent, 100, 30);
        lv_obj_set_size(parent, width - 106, height - 40);
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        const auto card = tracker::ui::item_card_view::create(parent, &lv_font_montserrat_14, &lv_font_montserrat_12, &lv_font_montserrat_16, width == 320 ? 36 : 28, width != 320);
        lv_label_set_text(card.name, selected_title.c_str());
        lv_label_set_text(card.distance, "Distance 3.6 km");
        lv_label_set_text(card.region, "~ Hangzhou / Xihu");
        lv_label_set_text(card.format, "KML | Routes");
        lv_label_set_text(card.counter, "2 / 18");
        int actions = 0;
        lv_obj_add_event_cb(
            card.actions, [](lv_event_t* e)
            { ++*static_cast<int*>(lv_event_get_user_data(e)); },
            LV_EVENT_CLICKED, &actions);
        lv_obj_send_event(card.actions, LV_EVENT_CLICKED, nullptr);
        assert(actions == 1);
        lv_obj_update_layout(parent);
        lv_refr_now(display);
        lv_area_t bounds{};
        lv_obj_get_coords(card.root, &bounds);
        for (auto* child : {card.name, card.distance, card.region, card.counter, card.previous, card.next, card.actions})
        {
            lv_area_t area{};
            lv_obj_get_coords(child, &area);
            assert(area.x1 >= bounds.x1 && area.x2 <= bounds.x2);
            assert(area.y1 >= bounds.y1 && area.y2 <= bounds.y2);
        }
        std::ofstream frame("tracker-" + std::to_string(width) + ".ppm", std::ios::binary);
        frame << "P6\n"
              << width << ' ' << height << "\n255\n";
        for (auto pixel : pixels)
        {
            const unsigned char rgb[] = {static_cast<unsigned char>(((pixel >> 11) & 31) * 255 / 31), static_cast<unsigned char>(((pixel >> 5) & 63) * 255 / 63), static_cast<unsigned char>((pixel & 31) * 255 / 31)};
            frame.write(reinterpret_cast<const char*>(rgb), 3);
        }
        std::printf("Tracker card %dx%d passed\n", width, height);
        lv_display_delete(display);
    }
    lv_deinit();
}
