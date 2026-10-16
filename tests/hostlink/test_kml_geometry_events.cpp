#include "track_sharing/kml_geometry_events.h"
#include <cassert>
#include <cstring>
using track_sharing::KmlGeometryEvents;
int main()
{
    KmlGeometryEvents parser;
    const auto ns = KmlGeometryEvents::kml;
    track_sharing::GeometryExtent extent;
    assert(parser.begin(ns, "kml") && parser.begin(ns, "Document") && parser.begin(ns, "Placemark"));
    assert(parser.begin(ns, "description") && parser.begin(ns, "coordinates") && parser.text("0,0", 3) && parser.end() && parser.end());
    assert(parser.begin(ns, "LineString") && parser.begin(ns, "coordinates") && parser.text("179,1 ", 6) && parser.text("-179,-2", 7) && parser.end() && parser.end());
    assert(parser.end() && parser.end() && parser.end() && parser.finish(extent) && extent.points == 2 && extent.west == 1790000000 && extent.east == -1790000000);
    KmlGeometryEvents invalid;
    assert(!invalid.begin("urn:fake", "kml") && !invalid.finish(extent));
    KmlGeometryEvents point;
    assert(point.begin(ns, "kml") && point.begin(ns, "Placemark") && point.begin(ns, "Point") && point.begin(ns, "coordinates") && point.text("0,0 1,1", 7));
    assert(!point.end() && !point.finish(extent));
    KmlGeometryEvents empty;
    assert(empty.begin(ns, "kml") && empty.begin(ns, "Placemark") && empty.begin(ns, "Point"));
    assert(!empty.end());
    KmlGeometryEvents missing;
    assert(missing.begin(ns, "kml") && missing.begin(ns, "Placemark") && missing.begin(KmlGeometryEvents::gx, "Track"));
    for (const char* when : {"2024-02-29T00:00:00Z", "", "2024-02-29T00:01:00+08:00"})
        assert(missing.begin(ns, "when") && missing.text(when, std::strlen(when)) && missing.end());
    for (const char* coord : {"1 2 3", "", "2 3 4"})
        assert(missing.begin(KmlGeometryEvents::gx, "coord") && missing.text(coord, std::strlen(coord)) && missing.end());
    assert(missing.end() && missing.end() && missing.end() && missing.finish(extent) && extent.points == 2);
    KmlGeometryEvents partial;
    assert(partial.begin(ns, "kml") && partial.begin(ns, "Placemark") && partial.begin(KmlGeometryEvents::gx, "Track") && partial.begin(KmlGeometryEvents::gx, "coord") && partial.text("1 2", 3));
    assert(!partial.end());
    KmlGeometryEvents duplicate;
    assert(duplicate.begin(ns, "kml") && duplicate.begin(ns, "Placemark") && duplicate.begin(ns, "Point") && duplicate.begin(ns, "coordinates") && duplicate.text("0,0", 3) && duplicate.end());
    assert(!duplicate.begin(ns, "coordinates"));
    for (const char* coordinates : {"0,0 1,0 1,1 0,0", "0,0 1,0 1,1 0.00000001,0"})
    {
        KmlGeometryEvents ring;
        assert(ring.begin(ns, "kml") && ring.begin(ns, "Placemark") && ring.begin(ns, "LinearRing") && ring.begin(ns, "coordinates") && ring.text(coordinates, std::strlen(coordinates)));
        const bool valid = ring.end();
        assert(valid == (std::string_view(coordinates) == "0,0 1,0 1,1 0,0"));
    }
}
