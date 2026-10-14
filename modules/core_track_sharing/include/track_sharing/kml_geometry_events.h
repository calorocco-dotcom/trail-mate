#pragma once
#include "track_sharing/coordinate_stream.h"
#include "track_sharing/track_datetime.h"
#include <cstring>
#include <new>
#include <string_view>
namespace track_sharing
{
// Input is a well-formed XML event stream with resolved namespace URIs and
// decoded text. Does not tokenize XML or resolve namespace declarations.
class KmlGeometryEvents
{
  public:
    KmlGeometryEvents() : stream_(bounds_, CoordinateText::KmlTuples) {}
    bool begin(std::string_view uri, std::string_view name)
    {
        if (failed_ || complete_ || depth_ >= 64) return fail();
        if (!depth_)
        {
            if (uri != kml || name != "kml") return fail();
            stack_[depth_++] = Kind::Root;
            return true;
        }
        const auto parent = kindAt(depth_ - 1);
        if (parent == Kind::Coordinates || parent == Kind::Coord || parent == Kind::When) return fail();
        Kind kind = Kind::Ignore;
        if (parent != Kind::Ignore && uri == kml)
        {
            if (container(parent) && name == "Document") kind = Kind::Document;
            else if (container(parent) && name == "Folder") kind = Kind::Folder;
            else if (container(parent) && name == "Placemark")
            {
                if (++features_ > 5000) return fail();
                kind = Kind::Placemark;
            }
            else if ((parent == Kind::Placemark || parent == Kind::Multi) && name == "MultiGeometry") kind = Kind::Multi;
            else if (parent == Kind::Placemark || parent == Kind::Multi)
            {
                if (name == "Point") kind = Kind::Point;
                else if (name == "LineString") kind = Kind::Line;
                else if (name == "Polygon") kind = Kind::Polygon;
                else if (name == "LinearRing") kind = Kind::Ring;
            }
            else if (parent == Kind::Polygon && (name == "outerBoundaryIs" || name == "innerBoundaryIs")) kind = Kind::Boundary;
            else if (parent == Kind::Boundary && name == "LinearRing") kind = Kind::Ring;
            else if ((parent == Kind::Point || parent == Kind::Line || parent == Kind::Ring) && name == "coordinates") kind = Kind::Coordinates;
            else if (parent == Kind::Track && name == "when") kind = Kind::When;
        }
        if (parent != Kind::Ignore && uri == gx)
        {
            if ((parent == Kind::Placemark || parent == Kind::Multi || parent == Kind::MultiTrack) && name == "Track") kind = Kind::Track;
            else if ((parent == Kind::Placemark || parent == Kind::Multi) && name == "MultiTrack") kind = Kind::MultiTrack;
            else if (parent == Kind::Track && name == "coord") kind = Kind::Coord;
        }
        if (kind == Kind::When)
        {
            when_size_ = 0;
            if (!stream_.idleScratch()) return fail();
        }
        if (kind == Kind::Track)
        {
            when_count_ = 0;
            coord_count_ = 0;
        }
        if (kind == Kind::Coordinates || kind == Kind::Coord)
        {
            if (kind == Kind::Coordinates && (static_cast<uint8_t>(stack_[depth_ - 1]) & 128)) return fail();
            start_points_ = bounds_.pointCount();
            stream_.~CoordinateStream();
            new (&stream_) CoordinateStream(bounds_, kind == Kind::Coord ? CoordinateText::GxCoordinate : CoordinateText::KmlTuples);
        }
        stack_[depth_++] = kind;
        return true;
    }
    bool text(const char* data, size_t length)
    {
        if (failed_ || !depth_) return false;
        const auto kind = kindAt(depth_ - 1);
        if (kind == Kind::When)
        {
            if (length > size_t(63 - when_size_)) return fail();
            std::memcpy(stream_.idleScratch() + when_size_, data, length);
            when_size_ += static_cast<uint8_t>(length);
        }
        if ((kind == Kind::Coordinates || kind == Kind::Coord) && !stream_.feed(data, length)) return fail();
        return true;
    }
    bool end()
    {
        if (failed_ || !depth_) return fail();
        --depth_;
        const auto kind = kindAt(depth_);
        if (kind == Kind::When && (!validTrackDateTime({stream_.idleScratch(), when_size_}, true) || ++when_count_ > 100000)) return fail();
        if (kind == Kind::Track && when_count_ && when_count_ != coord_count_) return fail();
        if ((kind == Kind::Point || kind == Kind::Line || kind == Kind::Ring || (kind == Kind::Track && !coord_count_)) && !(static_cast<uint8_t>(stack_[depth_]) & 128)) return fail();
        if (kind == Kind::Coordinates || kind == Kind::Coord)
        {
            if (!stream_.finish(kind == Kind::Coord)) return fail();
            const auto count = bounds_.pointCount() - start_points_;
            const auto parent = kindAt(depth_ - 1);
            if ((kind == Kind::Coord && count > 1) || (parent == Kind::Point && count != 1)) return fail();
            if (kind == Kind::Coord) ++coord_count_;
            if (parent == Kind::Line && count < 2) return fail();
            if (parent == Kind::Ring && (count < 4 || !stream_.closedRing())) return fail();
            if (count) stack_[depth_ - 1] = static_cast<Kind>(static_cast<uint8_t>(stack_[depth_ - 1]) | 128);
        }
        if (!depth_) complete_ = true;
        return true;
    }
    bool finish(GeometryExtent& result) const { return !failed_ && complete_ && !depth_ && bounds_.finish(result); }
    static constexpr std::string_view kml = "http://www.opengis.net/kml/2.2", gx = "http://www.google.com/kml/ext/2.2";

  private:
    enum class Kind : uint8_t
    {
        Ignore,
        Root,
        Document,
        Folder,
        Placemark,
        Multi,
        Point,
        Line,
        Polygon,
        Boundary,
        Ring,
        MultiTrack,
        Track,
        Coordinates,
        Coord,
        When
    };
    static bool container(Kind value) { return value == Kind::Root || value == Kind::Document || value == Kind::Folder; }
    Kind kindAt(size_t index) const { return static_cast<Kind>(static_cast<uint8_t>(stack_[index]) & 127); }
    bool fail()
    {
        failed_ = true;
        bounds_.add(INT64_MAX, 0);
        return false;
    }
    GeometryBounds bounds_;
    CoordinateStream stream_;
    Kind stack_[64]{};
    uint32_t features_ = 0, start_points_ = 0;
    uint32_t when_count_ = 0, coord_count_ = 0;
    uint8_t when_size_ = 0;
    uint8_t depth_ = 0;
    bool failed_ = false, complete_ = false;
};
static_assert(sizeof(KmlGeometryEvents) <= 288, "KML event consumer must not store feature/point lists");
} // namespace track_sharing
