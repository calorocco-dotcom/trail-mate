#pragma once
#include "track_sharing/kml_image_events.h"
#include <cstdint>
#include <cstring>
namespace ui::support
{
// Geometry change detection only, never an authenticity proof.
struct KmlRouteIdentity
{
    uint32_t counts[2]{};
    uint64_t hashes[2]{14695981039346656037ULL, 14695981039346656037ULL};
    static void mix(uint64_t& hash, const void* bytes, size_t count)
    {
        const auto* data = static_cast<const uint8_t*>(bytes);
        for (size_t i = 0; i < count; ++i)
        {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
    }
    void coordinate(bool gx, double lat, double lon, double altitude, bool has_altitude, bool broken)
    {
        auto& hash = hashes[gx ? 1 : 0];
        mix(hash, &lat, sizeof(lat));
        mix(hash, &lon, sizeof(lon));
        mix(hash, &altitude, sizeof(altitude));
        mix(hash, &has_altitude, sizeof(has_altitude));
        mix(hash, &broken, sizeof(broken));
        ++counts[gx ? 1 : 0];
    }
    void geometry(bool gx, bool begin, track_sharing::KmlAltitudeMode mode)
    {
        if (!begin) mix(hashes[gx ? 1 : 0], &mode, sizeof(mode));
    }
    bool operator==(const KmlRouteIdentity& other) const
    {
        return counts[0] == other.counts[0] && counts[1] == other.counts[1] && hashes[0] == other.hashes[0] && hashes[1] == other.hashes[1];
    }
};
} // namespace ui::support
