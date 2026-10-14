#pragma once

#include "platform/memory/psram_ptr.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/map_search_runtime.h"
#include "ui/localization.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace ui::support
{
// One current-item projection, allocated in PSRAM by its page. Reuse the
// Contacts region worker; this is not an administrative-boundary database.
struct CardLocation
{
    platform::ui::map_search::RegionResult region;
    uint32_t request = 0;
    double latitude = 0, longitude = 0;
    bool valid = false, submitted = false;
    std::string file;
};

inline void updateCardLocation(CardLocation*& location, bool has_position, double latitude, double longitude,
                               lv_obj_t* region, lv_obj_t* distance)
{
    const bool valid = has_position && std::isfinite(latitude) && std::isfinite(longitude) &&
                       std::abs(latitude) <= 90 && std::abs(longitude) <= 180;
    if (!valid)
    {
        if (location)
        {
            location->valid = false;
            location->submitted = false;
            location->request = 0;
        }
        i18n::set_label_text(region, "Position unknown");
        i18n::set_label_text(distance, "Distance unknown");
        return;
    }
    if (!location) location = platform::memory::createPsram<CardLocation>();
    if (location)
    {
        if (!location->valid || location->latitude != latitude || location->longitude != longitude)
        {
            location->region.path[0] = 0;
            location->region.status = platform::ui::map_search::Status::Idle;
            location->request = 0;
            location->submitted = false;
            location->valid = true;
            location->latitude = latitude;
            location->longitude = longitude;
        }
        if (!location->submitted)
        {
            location->request = platform::ui::map_search::submitRegion(latitude, longitude);
            location->submitted = location->request != 0;
        }
        if (location->request && !platform::ui::map_search::pollRegion(location->request, location->region))
        {
            location->submitted = false;
            location->request = 0;
            location->region.path[0] = 0;
        }
        if (location->region.path[0])
        {
            const std::string text = std::string("~ ") + location->region.path;
            // Do not reset the scrolling administrative path on each UI tick.
            if (std::strcmp(lv_label_get_text(region), text.c_str())) i18n::set_content_label_text_raw(region, text.c_str());
        }
        else i18n::set_label_text(region, location->request && location->region.status == platform::ui::map_search::Status::Searching
                                              ? "Location loading..."
                                              : "Region unknown");
    }
    else i18n::set_label_text(region, "Region unknown");
    const auto own = platform::ui::gps::get_data();
    if (!own.valid || !std::isfinite(own.lat) || !std::isfinite(own.lng) || std::abs(own.lat) > 90 || std::abs(own.lng) > 180)
    {
        i18n::set_label_text(distance, "My position unknown");
        return;
    }
    constexpr double rad = 3.14159265358979323846 / 180;
    const double dlat = (latitude - own.lat) * rad, dlon = (longitude - own.lng) * rad;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) + std::cos(latitude * rad) * std::cos(own.lat * rad) * std::sin(dlon / 2) * std::sin(dlon / 2);
    const double meters = 12742000 * std::asin(std::sqrt(std::clamp(a, 0.0, 1.0)));
    const auto text = meters < 1000 ? i18n::format("Distance %.0f m", meters) : i18n::format("Distance %.1f km", meters / 1000);
    if (std::strcmp(lv_label_get_text(distance), text.c_str())) i18n::set_content_label_text_raw(distance, text.c_str());
}
} // namespace ui::support
