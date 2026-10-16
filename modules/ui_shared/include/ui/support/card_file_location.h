#pragma once
#include "platform/memory/psram_ptr.h"
#include "ui/support/kml_card_title.h"
#include "ui/support/route_image_kml_reader.h"
#include <cmath>
#include <cstring>

namespace ui::support
{
// Read only the selected file's first point. The page retains two numbers, not
// route geometry, images or the complete file. Parsing work lives in PSRAM.
inline bool readCardFileLocation(const char* path, double& latitude, double& longitude, std::string* title = nullptr)
{
    const auto normalized = fs::normalize_path(path);
    if (normalized.empty()) return false;
    const auto dot = normalized.find_last_of('.');
    const auto extension = dot == std::string::npos ? std::string{} : normalized.substr(dot);
    struct Point
    {
        double latitude = 0, longitude = 0;
        bool found = false;
    } point;
    track_sharing::KmlImageCallbacks callbacks{};
    callbacks.context = &point;
    callbacks.coordinate = [](void* context, bool, double lat, double lon, double, bool, bool)
    {
        auto& result = *static_cast<Point*>(context);
        if (!result.found && std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90 && std::abs(lon) <= 180)
        {
            result.latitude = lat;
            result.longitude = lon;
            result.found = true;
        }
    };
    auto* work = platform::memory::createPsram<RouteImageKmlWork>(callbacks);
    if (!work) return false;
    auto* title_work = title && extension == ".kml" ? platform::memory::createPsram<KmlCardTitle>() : nullptr;
    if (title) title->clear();
    auto complete = [&]()
    { return point.found && (!title_work || title_work->hasDocumentTitle()); };
    lv_fs_file_t file;
    bool ok = lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) == LV_FS_RES_OK;
    if (ok)
    {
        // A card preview must not scan a large file on the UI thread.
        uint32_t remaining = 65536;
        size_t attribute_size = 0;
        bool point_tag = false, latitude_attribute = false, longitude_attribute = false, has_latitude = false, has_longitude = false;
        yxml_t xml;
        yxml_init(&xml, work->names, sizeof(work->names));
        while (ok && remaining && !complete())
        {
            uint32_t count = 0;
            ok = lv_fs_read(&file, work->chunk, std::min<uint32_t>(remaining, sizeof(work->chunk)), &count) == LV_FS_RES_OK && count <= sizeof(work->chunk);
            if (!ok || !count) break;
            remaining -= count;
            if (extension == ".bin")
            {
#if defined(ARDUINO_ARCH_ESP32)
                int32_t lat = 0, lon = 0;
                if (count < 8) break;
                std::memcpy(&lat, work->chunk, 4);
                std::memcpy(&lon, work->chunk + 4, 4);
                point.latitude = lat / 10000000.0;
                point.longitude = lon / 10000000.0;
#else
                if (count < 24) break;
                std::memcpy(&point.latitude, work->chunk + 8, 8);
                std::memcpy(&point.longitude, work->chunk + 16, 8);
#endif
                point.found = true;
                break;
            }
            if (extension == ".csv")
            {
                work->source[0] = 0;
                size_t length = 0;
                // Firmware CSV starts with lat,lon; the desktop writer starts
                // with timestamp,lat,lon. The header identifies that format.
                const bool timestamp_first = std::strncmp(work->chunk, "timestamp", 9) == 0 || std::strncmp(work->chunk, "epoch_s", 7) == 0;
                char* line = static_cast<char*>(std::memchr(work->chunk, '\n', count));
                if (!line) break;
                ++line;
                while (line < work->chunk + count && *line != '\n' && length < 128) work->source[length++] = *line++;
                work->source[length] = 0;
                char* start = work->source;
                if (timestamp_first)
                {
                    start = std::strchr(start, ',');
                    if (!start) break;
                    ++start;
                }
                char* end = nullptr;
                point.latitude = std::strtod(start, &end);
                if (end == start || *end != ',') break;
                start = end + 1;
                point.longitude = std::strtod(start, &end);
                point.found = end != start && (*end == ',' || *end == '\0');
                break;
            }
            for (uint32_t i = 0; i < count && ok && !complete(); ++i)
            {
                const auto byte = static_cast<uint8_t>(work->chunk[i]);
                if (extension == ".kml")
                {
                    if (title_work) title_work->feed(byte);
                    ok = work->parser.feed(&byte, 1);
                    continue;
                }
                if (extension != ".gpx")
                {
                    ok = false;
                    break;
                }
                const auto event = yxml_parse(&xml, byte);
                if (event < 0)
                {
                    ok = false;
                    break;
                }
                if (event == YXML_ELEMSTART)
                {
                    const char* name = std::strrchr(xml.elem, ':');
                    name = name ? name + 1 : xml.elem;
                    point_tag = std::strcmp(name, "trkpt") == 0 || std::strcmp(name, "rtept") == 0 || std::strcmp(name, "wpt") == 0;
                    has_latitude = has_longitude = false;
                }
                else if (point_tag && event == YXML_ATTRSTART)
                {
                    latitude_attribute = std::strcmp(xml.attr, "lat") == 0;
                    longitude_attribute = std::strcmp(xml.attr, "lon") == 0;
                    attribute_size = 0;
                }
                else if (point_tag && event == YXML_ATTRVAL && (latitude_attribute || longitude_attribute))
                {
                    if (attribute_size >= 64)
                    {
                        ok = false;
                        break;
                    }
                    work->source[attribute_size++] = xml.data[0];
                }
                else if (point_tag && event == YXML_ATTREND && (latitude_attribute || longitude_attribute))
                {
                    work->source[attribute_size] = 0;
                    char* end = nullptr;
                    const double value = std::strtod(work->source, &end);
                    if (end == work->source || *end || !std::isfinite(value))
                    {
                        ok = false;
                        break;
                    }
                    if (latitude_attribute)
                    {
                        point.latitude = value;
                        has_latitude = true;
                    }
                    else
                    {
                        point.longitude = value;
                        has_longitude = true;
                    }
                    point.found = has_latitude && has_longitude;
                }
            }
        }
        lv_fs_close(&file);
    }
    platform::memory::destroyPsram(work);
    if (title_work) *title = title_work->title();
    platform::memory::destroyPsram(title_work);
    if (!ok || !point.found || !std::isfinite(point.latitude) || !std::isfinite(point.longitude) ||
        std::abs(point.latitude) > 90 || std::abs(point.longitude) > 180) return false;
    latitude = point.latitude;
    longitude = point.longitude;
    return true;
}
} // namespace ui::support
