#include "lvgl.h"
#include "platform/esp/arduino_common/map_poi/cjson_poi_parser.h"
#include "ui_map_runtime/map_poi/annotation_frame.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

bool measure(void* context, const char* text, std::size_t bytes, bool ellipsis, int16_t& width, int16_t& height)
{
    char value[80];
    std::memcpy(value, text, bytes);
    if (ellipsis)
    {
        std::memcpy(value + bytes, "\xE2\x80\xA6", 3);
        bytes += 3;
    }
    value[bytes] = 0;
    lv_point_t size;
    lv_text_get_size(&size, value, static_cast<lv_font_t*>(context), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    width = size.x;
    height = size.y;
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 6) return 2;
    const std::filesystem::path maps(argv[1]);
    const int zoom = std::stoi(argv[3]);
    const double lon = std::stod(argv[4]), lat = std::stod(argv[5]);
    const double scale = 256.0 * (1 << zoom), pi = 3.14159265358979323846;
    // Host fixture projects the exported XYZ data to a 480x222 north-up view.
    // Device projection remains owned by the existing map backend.
    const double left = (lon + 180) / 360 * scale - 240;
    const double top = (1 - std::asinh(std::tan(lat * pi / 180)) / pi) / 2 * scale - 111;
    struct Source
    {
        ui::map_poi::Record record;
        int x, y;
    };
    std::vector<Source> records;
    platform::esp::arduino_common::map_poi::CJsonPoiParser parser;
    for (int x = static_cast<int>(std::floor(left / 256)); x <= static_cast<int>(std::floor((left + 480) / 256)); ++x)
        for (int y = static_cast<int>(std::floor(top / 256)); y <= static_cast<int>(std::floor((top + 222) / 256)); ++y)
        {
            auto path = maps / "poi/index" / std::to_string(zoom) / std::to_string(x) / (std::to_string(y) + ".jsonl");
            std::ifstream input(path);
            std::string line;
            while (std::getline(input, line))
            {
                Source value{};
                value.x = x;
                value.y = y;
                if (!parser.record(line.data(), line.size(), value.record)) return 3;
                records.push_back(value);
            }
        }
    lv_init();
    std::ifstream binary(argv[2], std::ios::binary);
    std::vector<char> fontBytes((std::istreambuf_iterator<char>(binary)), std::istreambuf_iterator<char>());
    auto* subset = lv_binfont_create_from_buffer(fontBytes.data(), fontBytes.size());
    if (!subset) return 4;
    lv_font_t font = lv_font_montserrat_14;
    font.fallback = subset;
    ui::map_poi::AnnotationFrame frame([](std::size_t size, void*)
                                       { return std::malloc(size); },
                                       [](void* data, void*)
                                       { std::free(data); });
    ui::map::MapPoiSnapshot metadata;
    metadata.enabled = metadata.header.valid = true;
    metadata.width = 480;
    metadata.height = 222;
    metadata.view_key = 1;
    metadata.compatibility_key = zoom;
    if (!frame.begin(records.size(), metadata)) return 5;
    for (const auto& source : records)
    {
        const auto& r = source.record;
        ui::map_poi::AnnotationCandidate c;
        c.key = r.key;
        c.feature_key = r.feature_key;
        c.name = r.name;
        c.category = r.category;
        c.kind = r.kind;
        c.priority = r.priority;
        c.x = static_cast<int16_t>((r.lon + 180) / 360 * scale - left);
        c.y = static_cast<int16_t>((1 - std::asinh(std::tan(r.lat * pi / 180)) / pi) / 2 * scale - top);
        c.path_points = r.path_points;
        for (int p = 0; p < r.path_points; ++p)
        {
            c.path[p * 2] = static_cast<int16_t>(source.x * 256.0 + r.path[p * 2] - left);
            c.path[p * 2 + 1] = static_cast<int16_t>(source.y * 256.0 + r.path[p * 2 + 1] - top);
        }
        frame.add(c);
    }
    ui::map_poi::AnnotationLayoutOptions options;
    options.width = 480;
    options.height = 222;
    options.max_labels = ui::map_poi::annotation_label_budget(zoom);
    options.road_reservation = zoom <= 6 ? 0 : zoom <= 11 ? 3
                                                          : 8;
    options.place_reservation = zoom <= 11 ? 6 : 2;
    if (!frame.finish(options, measure, &font)) return 6;
    const auto& result = frame.result();
    std::cout << "z=" << zoom << " records=" << records.size() << " admitted=" << frame.candidate_count()
              << " roads=" << result.road_labels << " places=" << result.place_labels << " pois=" << result.poi_labels
              << " unnamed=" << result.unnamed_markers << " collisions=" << result.collisions << '\n';
    for (std::size_t i = 0; i < frame.snapshot().item_count; ++i)
    {
        const auto& item = frame.snapshot().items[i];
        std::cout << static_cast<int>(item.kind) << '\t' << item.text_x << '\t' << item.text_y << '\t' << item.label.c_str() << '\n';
    }
    frame.clear();
    lv_binfont_destroy(subset);
    lv_deinit();
}
