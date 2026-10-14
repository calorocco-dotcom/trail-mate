#pragma once
#include "gps/kml/html_image_source_stream.h"
#include "track_sharing/kml_ui_coordinate_stream.h"
#include "track_sharing/track_datetime.h"
#include "track_sharing/xml_namespace_scope.h"
#include <optional>

namespace track_sharing
{
enum class KmlAltitudeMode : uint8_t
{
    ClampGround,
    RelativeGround,
    Absolute,
    ClampSeaFloor,
    RelativeSeaFloor
};
struct KmlImageCallbacks
{
    void* context = nullptr;
    void (*beginFeature)(void*) = nullptr;
    // Source is XML-decoded, HTML-entity-encoded and borrowed until return.
    // False stops images in this description, not XML validation.
    bool (*image)(void*, std::string_view) = nullptr;
    void (*endFeature)(void*, bool, double, double) = nullptr;
    // Optional LineString/gx:Track preview points. Point/polygon coordinates
    // locate images but are not added to a walking route.
    void (*coordinate)(void*, bool, double, double, double, bool, bool) = nullptr;
    // Mode can follow coordinates. Consumers finalize height semantics at end.
    void (*geometry)(void*, bool, bool, KmlAltitudeMode) = nullptr;
};

// Description images are emitted in document order. The caller associates its
// feature's image slice at endFeature, so geometry/description order is neutral.
// This scanner is for UI association, not geometry or author validation.
class KmlImageEvents
{
  public:
    KmlImageEvents(char* source, size_t capacity, KmlImageCallbacks callbacks) : html_(source, capacity), callbacks_(callbacks), scalar_(source), scalar_capacity_(capacity) {}
    bool begin(XmlNamespace space, std::string_view local)
    {
        if (depth_ >= 64) return false;
        const auto parent = depth_ ? nodes_[depth_ - 1] : Node::Ignored;
        Node node = Node::Ignored;
        if (!depth_)
        {
            if (root_ || local != "kml" || (space != XmlNamespace::Kml && space != XmlNamespace::None)) return false;
            root_ = true;
            legacy_ = space == XmlNamespace::None;
            node = Node::Root;
        }
        else
        {
            // description is a string (CDATA or escaped HTML), never an XML
            // geometry container. Do not concatenate around real child nodes.
            if (parent == Node::Description || parent == Node::Coordinates || parent == Node::GxCoordinate || parent == Node::When || parent == Node::AltitudeMode) return false;
            const bool kml = space == XmlNamespace::Kml || (legacy_ && space == XmlNamespace::None);
            const bool feature_parent = parent == Node::Root || parent == Node::Container;
            if (kml && feature_parent && (local == "Document" || local == "Folder")) node = Node::Container;
            else if (kml && feature_parent && local == "Placemark")
            {
                node = Node::Placemark;
                located_ = false;
                if (callbacks_.beginFeature) callbacks_.beginFeature(callbacks_.context);
            }
            else if (kml && parent == Node::Placemark && local == "description")
            {
                node = Node::Description;
                html_.reset();
            }
            else if (kml && (parent == Node::Placemark || parent == Node::MultiGeometry) && local == "MultiGeometry") node = Node::MultiGeometry;
            else if (kml && (parent == Node::Placemark || parent == Node::MultiGeometry) && local == "Point") node = Node::Point;
            else if (kml && (parent == Node::Placemark || parent == Node::MultiGeometry) && local == "LineString") node = Node::Line;
            else if (kml && (parent == Node::Placemark || parent == Node::MultiGeometry) && local == "Polygon") node = Node::Polygon;
            else if (kml && parent == Node::Polygon && (local == "outerBoundaryIs" || local == "innerBoundaryIs")) node = Node::Boundary;
            else if (kml && parent == Node::Boundary && local == "LinearRing") node = Node::Ring;
            else if (space == XmlNamespace::Gx && (parent == Node::Placemark || parent == Node::MultiGeometry) && local == "MultiTrack") node = Node::MultiTrack;
            else if (space == XmlNamespace::Gx && (parent == Node::Placemark || parent == Node::MultiGeometry || parent == Node::MultiTrack) && local == "Track") node = Node::Track;
            else if (kml && (parent == Node::Point || parent == Node::Line || parent == Node::Ring) && local == "coordinates") node = Node::Coordinates;
            else if (space == XmlNamespace::Gx && parent == Node::Track && local == "coord") node = Node::GxCoordinate;
            else if (kml && parent == Node::Track && local == "when") node = Node::When;
            else if ((kml || space == XmlNamespace::Gx) && (parent == Node::Line || parent == Node::Track) && local == "altitudeMode")
            {
                if (altitude_seen_) return false;
                node = Node::AltitudeMode;
                scalar_size_ = 0;
                altitude_seen_ = true;
                gx_altitude_ = space == XmlNamespace::Gx;
            }
            if (node == Node::Track) when_count_ = coordinate_count_ = 0;
            if (node == Node::When)
            {
                if (++when_count_ > 100000) return false;
                scalar_size_ = 0;
            }
            if (node == Node::GxCoordinate && ++coordinate_count_ > 100000) return false;
            if (node == Node::Line || node == Node::Track)
            {
                pending_break_ = true;
                altitude_mode_ = KmlAltitudeMode::ClampGround;
                altitude_seen_ = false;
                if (callbacks_.geometry) callbacks_.geometry(callbacks_.context, node == Node::Track, true, altitude_mode_);
            }
            if (node == Node::Coordinates || node == Node::GxCoordinate)
            {
                coordinate_emitted_ = false;
                coordinate_geometry_ = parent;
                coordinate_.emplace(node == Node::GxCoordinate, this,
                                    [](void* p, double lat, double lon, double altitude, bool has_altitude)
                                    { return static_cast<KmlImageEvents*>(p)->point(lat, lon, altitude, has_altitude); });
            }
        }
        nodes_[depth_++] = node;
        return true;
    }
    bool text(const char* text, size_t size)
    {
        if (!depth_) return false;
        const auto node = nodes_[depth_ - 1];
        if (node == Node::Description)
        {
            html_.feed({text, size}, [&](std::string_view source)
                       { return !callbacks_.image || callbacks_.image(callbacks_.context, source); });
        }
        else if (node == Node::Coordinates || node == Node::GxCoordinate) return coordinate_->feed(text, size);
        else if (node == Node::When || node == Node::AltitudeMode)
        {
            if (!text || size > 63 - scalar_size_ || scalar_size_ + size > scalar_capacity_) return false;
            std::memcpy(scalar_ + scalar_size_, text, size);
            scalar_size_ += size;
        }
        return true;
    }
    bool end()
    {
        if (!depth_) return false;
        const auto node = nodes_[--depth_];
        if (node == Node::Coordinates || node == Node::GxCoordinate)
        {
            if (!coordinate_->finish()) return false;
            if (node == Node::GxCoordinate && !coordinate_emitted_) pending_break_ = true;
            coordinate_.reset(); // empty gx:coord remains an unlocated sample
        }
        else if (node == Node::When && !validTrackDateTime({scalar_, scalar_size_}, true)) return false;
        else if (node == Node::Track && when_count_ && when_count_ != coordinate_count_) return false;
        else if (node == Node::AltitudeMode)
        {
            auto value = std::string_view(scalar_, scalar_size_);
            const auto whitespace = [](char c)
            { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
            while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
            if (!gx_altitude_ && value == "absolute") altitude_mode_ = KmlAltitudeMode::Absolute;
            else if (!gx_altitude_ && value == "clampToGround") altitude_mode_ = KmlAltitudeMode::ClampGround;
            else if (!gx_altitude_ && value == "relativeToGround") altitude_mode_ = KmlAltitudeMode::RelativeGround;
            else if (gx_altitude_ && value == "clampToSeaFloor") altitude_mode_ = KmlAltitudeMode::ClampSeaFloor;
            else if (gx_altitude_ && value == "relativeToSeaFloor") altitude_mode_ = KmlAltitudeMode::RelativeSeaFloor;
            else return false;
        }
        if ((node == Node::Line || node == Node::Track) && callbacks_.geometry)
            callbacks_.geometry(callbacks_.context, node == Node::Track, false, altitude_mode_);
        else if (node == Node::Placemark && callbacks_.endFeature)
            callbacks_.endFeature(callbacks_.context, located_, latitude_, longitude_);
        return true;
    }
    bool finish() const { return root_ && depth_ == 0; }

  private:
    enum class Node : uint8_t
    {
        Ignored,
        Root,
        Container,
        Placemark,
        Description,
        MultiGeometry,
        Point,
        Line,
        Ring,
        Polygon,
        Boundary,
        Track,
        MultiTrack,
        Coordinates,
        GxCoordinate,
        When,
        AltitudeMode
    };
    bool point(double lat, double lon, double altitude, bool has_altitude)
    {
        if (++point_count_ > 100000) return false;
        coordinate_emitted_ = true;
        if (!located_) longitude_ = lon, latitude_ = lat, located_ = true;
        if (coordinate_geometry_ == Node::Line || coordinate_geometry_ == Node::Track)
        {
            if (callbacks_.coordinate) callbacks_.coordinate(callbacks_.context, coordinate_geometry_ == Node::Track, lat, lon, altitude, has_altitude, pending_break_);
            pending_break_ = false;
        }
        return true;
    }
    gps::kml::HtmlImageSourceStream html_;
    KmlImageCallbacks callbacks_;
    std::optional<KmlUiCoordinateStream> coordinate_;
    Node nodes_[64]{};
    double latitude_ = 0, longitude_ = 0;
    uint8_t depth_ = 0;
    Node coordinate_geometry_ = Node::Ignored;
    uint32_t point_count_ = 0;
    char* scalar_;
    size_t scalar_capacity_, scalar_size_ = 0;
    uint32_t when_count_ = 0, coordinate_count_ = 0;
    KmlAltitudeMode altitude_mode_ = KmlAltitudeMode::ClampGround;
    bool altitude_seen_ = false, gx_altitude_ = false;
    bool root_ = false, legacy_ = false, located_ = false;
    bool pending_break_ = true, coordinate_emitted_ = false;
};
static_assert(sizeof(KmlImageEvents) <= 480, "Image/time events retain one tuple and borrow scalar storage");
} // namespace track_sharing
