#pragma once
#include "tmap/reader.h"
#include <cstdio>
#include <cstring>

namespace navigation
{
// Output is sequential and bounded. The platform sink owns file I/O and the
// temporary-file/rename lifecycle; it must not retain a borrowed write slice.
class RouteKmlWriter
{
  public:
    using Write = bool (*)(void*, const char*, size_t);
    bool begin(Write write, void* context, const char* origin_name, tmap::GeoPoint origin,
               const char* destination_name, tmap::GeoPoint destination)
    {
        write_ = write;
        context_ = context;
        used_ = count_ = 0;
        valid_ = write && origin_name && destination_name && pointValid(origin) && pointValid(destination);
        if (!valid_) return false;
        destination_ = destination;
        // Keep endpoint strings in the caller's request until finish().
        destination_name_ = destination_name;
        return text("<?xml version=\"1.0\" encoding=\"UTF-8\"?><kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><name>") &&
               escaped(origin_name) && text(" - ") && escaped(destination_name) && text("</name>") &&
               endpoint("origin", origin_name, origin) &&
               text("<Placemark><name>Navigation</name><LineString><tessellate>1</tessellate><coordinates>");
    }
    bool point(tmap::GeoPoint value)
    {
        if (!valid_ || !pointValid(value)) return valid_ = false;
        if (!coordinate(value) || !text(" ")) return false;
        ++count_;
        return true;
    }
    bool finish()
    {
        if (!valid_ || count_ < 2) return valid_ = false;
        const bool complete = text("</coordinates></LineString></Placemark>") &&
                              endpoint("destination", destination_name_, destination_) && text("</Document></kml>") && flush();
        valid_ = false;
        return complete;
    }
    size_t pointCount() const { return count_; }

  private:
    static bool pointValid(tmap::GeoPoint value)
    {
        return value.latitude >= -900000000 && value.latitude <= 900000000 &&
               value.longitude >= -1800000000 && value.longitude <= 1800000000;
    }
    bool flush()
    {
        if (!valid_) return false;
        if (used_ && !write_(context_, bytes_, used_)) return valid_ = false;
        used_ = 0;
        return true;
    }
    bool text(const char* value)
    {
        if (!valid_ || !value) return false;
        while (*value)
        {
            if (used_ == sizeof(bytes_) && !flush()) return false;
            bytes_[used_++] = *value++;
        }
        return true;
    }
    bool escaped(const char* value)
    {
        for (; *value; ++value)
        {
            const char* replacement = nullptr;
            switch (*value)
            {
            case '&':
                replacement = "&amp;";
                break;
            case '<':
                replacement = "&lt;";
                break;
            case '>':
                replacement = "&gt;";
                break;
            case '"':
                replacement = "&quot;";
                break;
            case '\'':
                replacement = "&apos;";
                break;
            default:
                break;
            }
            if (replacement)
            {
                if (!text(replacement)) return false;
            }
            else
            {
                if (used_ == sizeof(bytes_) && !flush()) return false;
                bytes_[used_++] = *value;
            }
        }
        return valid_;
    }
    bool coordinate(tmap::GeoPoint value)
    {
        const auto latitude = value.latitude < 0 ? -int64_t(value.latitude) : int64_t(value.latitude);
        const auto longitude = value.longitude < 0 ? -int64_t(value.longitude) : int64_t(value.longitude);
        std::snprintf(coordinate_, sizeof(coordinate_), "%s%lu.%07lu,%s%lu.%07lu",
                      value.longitude < 0 ? "-" : "", static_cast<unsigned long>(longitude / 10000000),
                      static_cast<unsigned long>(longitude % 10000000), value.latitude < 0 ? "-" : "",
                      static_cast<unsigned long>(latitude / 10000000), static_cast<unsigned long>(latitude % 10000000));
        return text(coordinate_);
    }
    bool endpoint(const char* role, const char* name, tmap::GeoPoint value)
    {
        return text("<Placemark><name>") && escaped(name) && text("</name><ExtendedData><Data name=\"trailmate:role\"><value>") &&
               text(role) && text("</value></Data></ExtendedData><Point><coordinates>") && coordinate(value) &&
               text("</coordinates></Point></Placemark>");
    }
    Write write_ = nullptr;
    void* context_ = nullptr;
    const char* destination_name_ = nullptr;
    tmap::GeoPoint destination_{};
    size_t used_ = 0, count_ = 0;
    char bytes_[192]{}, coordinate_[64]{};
    bool valid_ = false;
};
static_assert(sizeof(RouteKmlWriter) <= 320, "Navigation serialization has one small output buffer");
} // namespace navigation
