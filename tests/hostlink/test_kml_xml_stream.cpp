#include "track_sharing/kml_xml_stream.h"
#include <cassert>
#include <string>
#include <utility>
static char names[2048];
int main()
{
    const std::string xml = "<k:kml xmlns:k=\"http://www.opengis.net/kml/2.2\"><k:Document><k:Placemark><k:description><![CDATA[<coordinates>0,0</coordinates>]]></k:description><k:LineString><k:coordinates>&#49;79,1 -179,-2</k:coordinates></k:LineString></k:Placemark></k:Document></k:kml>";
    for (size_t split = 0; split <= xml.size(); ++split)
    {
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        assert(parser.feed(reinterpret_cast<const uint8_t*>(xml.data()), split) && parser.feed(reinterpret_cast<const uint8_t*>(xml.data() + split), xml.size() - split) && parser.finish(extent));
        assert(extent.points == 2 && extent.west == 1790000000 && extent.east == -1790000000);
    }
    for (const std::string text : {"<!DOCTYPE kml><kml/>", "<kml><a></kml>", "<kml xmlns=\"urn:fake\"/>", "<x:kml/>", "<kml>\xc0\xaf</kml>"})
    {
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        assert(!(parser.feed(reinterpret_cast<const uint8_t*>(text.data()), text.size()) && parser.finish(extent)));
    }
    for (const auto& sample : {std::pair<const char*, bool>{"id=\"1\" id=\"2\"", false}, {"a:id=\"1\" b:id=\"2\" xmlns:a=\"urn:same\" xmlns:b=\"urn:same\"", false}, {"a:id=\"1\" b:id=\"2\" xmlns:a=\"urn:a\" xmlns:b=\"urn:b\"", true}, {"id=\"1\" a:id=\"2\" xmlns:a=\"http://www.opengis.net/kml/2.2\"", true}, {"a:id=\"1\"", false}})
    {
        const std::string source = std::string("<kml xmlns=\"http://www.opengis.net/kml/2.2\" ") + sample.first + "><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>";
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        const bool valid = parser.feed(reinterpret_cast<const uint8_t*>(source.data()), source.size()) && parser.finish(extent);
        assert(valid == sample.second);
    }
    for (const auto& sample : {std::pair<const char*, bool>{"中文:属性=\"1\" xmlns:中文=\"urn:a\"", true}, {"\xcc\x80id=\"1\"", false}})
    {
        const std::string source = std::string("<kml xmlns=\"http://www.opengis.net/kml/2.2\" ") + sample.first + "><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>";
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        assert((parser.feed(reinterpret_cast<const uint8_t*>(source.data()), source.size()) && parser.finish(extent)) == sample.second);
    }
    const std::string body = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>";
    const std::string declaration = "\xef\xbb\xbf<?xml version='1.0' encoding = 'utf-8' standalone=\"no\"?>";
    const std::string declared = declaration + body;
    for (size_t split = 0; split <= declared.size(); ++split)
    {
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        assert(parser.feed(reinterpret_cast<const uint8_t*>(declared.data()), split) && parser.feed(reinterpret_cast<const uint8_t*>(declared.data() + split), declared.size() - split) && parser.finish(extent));
    }
    for (const char* declaration : {"<?xml version=\"1.1\"?>", "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>", "<?xml version=\"1.0\" encoding=\"UTF-16\"?>"})
    {
        const std::string source = declaration + body;
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        assert(!(parser.feed(reinterpret_cast<const uint8_t*>(source.data()), source.size()) && parser.finish(extent)));
    }
    for (unsigned times = 0; times < 3; ++times)
    {
        std::string source = "<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\"><Placemark><gx:Track>";
        for (unsigned i = 0; i < times; ++i) source += "<when>2026-10-11T00:00:00Z</when>";
        source += "<gx:coord>1 2 3</gx:coord></gx:Track></Placemark></kml>";
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        assert((parser.feed(reinterpret_cast<const uint8_t*>(source.data()), source.size()) && parser.finish(extent)) == (times < 2));
    }
    const std::string tracks = "<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\"><Placemark><gx:MultiTrack><gx:Track><when>2026-10-11T00:00:00Z</when><gx:coord>1 2 3</gx:coord></gx:Track><gx:Track><gx:coord>2 3 4</gx:coord></gx:Track></gx:MultiTrack></Placemark></kml>";
    track_sharing::KmlXmlStream trackParser(names, sizeof(names));
    track_sharing::GeometryExtent trackExtent;
    assert(trackParser.feed(reinterpret_cast<const uint8_t*>(tracks.data()), tracks.size()) && trackParser.finish(trackExtent) && trackExtent.points == 2);
    for (const auto& sample : {std::pair<const char*, bool>{"2026-02-31T00:00:00Z", false}, {"2024-02-29T00:00:00+14:00", true}, {"2024-02-29T00:00:00+14:01", false}, {"", true}})
    {
        const std::string source = std::string("<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\"><Placemark><gx:Track><when>") + sample.first + "</when><gx:coord>1 2 3</gx:coord></gx:Track></Placemark></kml>";
        track_sharing::KmlXmlStream parser(names, sizeof(names));
        track_sharing::GeometryExtent extent;
        bool valid = true;
        for (const auto ch : source)
            if (!parser.feed(reinterpret_cast<const uint8_t*>(&ch), 1))
            {
                valid = false;
                break;
            }
        valid = valid && parser.finish(extent);
        assert(valid == sample.second);
    }
}
