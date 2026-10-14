#include "track_sharing/kml_image_xml_stream.h"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

struct Image
{
    std::string source;
    bool located = false;
    double lat = 0, lon = 0;
};
struct Result
{
    struct Coordinate
    {
        double lat, lon;
        bool gx, break_before;
    };
    std::vector<Image> images;
    std::vector<Coordinate> coordinates;
    size_t feature_start = 0;
    unsigned features = 0;
    track_sharing::KmlImageCallbacks callbacks()
    {
        return {this,
                [](void* p)
                { auto& r = *static_cast<Result*>(p); r.feature_start = r.images.size(); ++r.features; },
                [](void* p, std::string_view source)
                { auto& r = *static_cast<Result*>(p); r.images.push_back({gps::kml::decodeImageSource(source)}); return true; },
                [](void* p, bool located, double lat, double lon)
                { auto& r = *static_cast<Result*>(p); for (size_t i = r.feature_start; i < r.images.size(); ++i) r.images[i].located = located, r.images[i].lat = lat, r.images[i].lon = lon; },
                [](void* p, bool gx, double lat, double lon, double, bool, bool break_before)
                { static_cast<Result*>(p)->coordinates.push_back({lat, lon, gx, break_before}); }};
    }
};
static bool parse(std::string_view xml, size_t chunk, Result& result)
{
    char names[4096], source[4096];
    track_sharing::KmlImageXmlStream parser(names, sizeof(names), source, sizeof(source), result.callbacks());
    for (size_t i = 0; i < xml.size(); i += chunk)
        if (!parser.feed(reinterpret_cast<const uint8_t*>(xml.data() + i), std::min(chunk, xml.size() - i))) return false;
    return parser.finish();
}
static std::vector<std::string> html(std::string_view text, size_t chunk, size_t capacity = 4096, bool stop = false)
{
    char source[4096];
    gps::kml::HtmlImageSourceStream parser(source, capacity);
    std::vector<std::string> found;
    for (size_t i = 0; i < text.size(); i += chunk)
        parser.feed(text.substr(i, chunk), [&](std::string_view value)
                    { found.push_back(gps::kml::decodeImageSource(value)); return !stop; });
    return found;
}
int main()
{
    const std::string input = "<?xml version='1.0' encoding='UTF-8'?><k:kml xmlns:k='http://www.opengis.net/kml/2.2' xmlns:g='http://www.google.com/kml/ext/2.2'><k:Document><k:Folder>"
                              "<k:Placemark><k:Point><k:coordinates>120.1234567,30.5,7</k:coordinates></k:Point><k:description><![CDATA[<!-- <img src=bad> --><img\nsrc='a&amp;quot;山.jpg'><img alt='>' SRC=two.jpg>]]></k:description></k:Placemark>"
                              "<k:Placemark><k:description>&lt;img src=&quot;b&amp;amp;c.jpg&quot;&gt;</k:description><g:Track><g:coord/><g:coord>121 31 12</g:coord></g:Track></k:Placemark>"
                              "<k:Placemark><k:description><![CDATA[<img src=unlocated>]]></k:description></k:Placemark>"
                              "</k:Folder></k:Document></k:kml>";
    for (size_t chunk = 1; chunk <= input.size(); ++chunk)
    {
        Result r;
        assert(parse(input, chunk, r));
        assert(r.features == 3 && r.images.size() == 4);
        assert(r.images[0].source == "a&quot;山.jpg" && r.images[1].source == "two.jpg");
        assert(r.images[0].located && std::abs(r.images[0].lat - 30.5) < 1e-8 && std::abs(r.images[0].lon - 120.1234567) < 1e-8);
        assert(r.images[1].located && r.images[1].lat == r.images[0].lat);
        assert(r.images[2].source == "b&c.jpg" && r.images[2].located && std::abs(r.images[2].lon - 121) < 1e-8);
        assert(!r.images[3].located);
    }
    const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
        {"<img src=x", {}}, {"<img src='' src=bad><img src src=bad>", {}}, {"<img data-src=x srcset=y SRC='a>b' src=bad><img src=b.jpg/>", {"a>b", "b.jpg/"}}, {"<script>if(a < b) { '<img src=bad>' }</scripture><img src=bad></SCRIPT><img src=good>", {"good"}}, {"<!--<img src=bad>--><style><img src=bad></style><textarea><img src=bad></textarea><title><img src=bad></title><IMG src='山😀'>", {"山😀"}}, {"<a data-x='<img src=bad>'><img bad<attribute src=x><img src=good>", {"good"}}, {"<img alt='" + std::string(100000, 'x') + "' src=good>", {"good"}}, {"<img src='" + std::string(4097, 'x') + "'><img src=good>", {"good"}}};
    for (const auto& item : cases)
        for (size_t chunk : {size_t(1), size_t(2), size_t(17), size_t(256), item.first.size()}) assert(html(item.first, chunk) == item.second);
    assert(html("<img src=1234><img src=12345><img src=x>", 1, 4) == std::vector<std::string>({"1234", "x"}));
    assert(html("<img src=a><img src=b>", 1, 4096, true) == std::vector<std::string>({"a"}));
    char source[16];
    gps::kml::HtmlImageSourceStream reset(source, sizeof(source));
    std::vector<std::string> found;
    auto collect = [&](std::string_view v)
    { found.emplace_back(v); return true; };
    reset.feed("<img src='partial", collect);
    reset.reset();
    reset.feed("rest'><img src=good>", collect);
    assert(found == std::vector<std::string>({"good"}));

    const auto check = [](const std::string& doc, bool expected, size_t images)
    {
        for (size_t chunk : {size_t(1), size_t(7), doc.size()})
        {
            Result r;
            assert(parse(doc, chunk, r) == expected);
            if (expected) assert(r.images.size() == images);
        }
    };
    check("<kml><Placemark><description><![CDATA[<img src=legacy>]]></description><Point><coordinates>1,2</coordinates></Point></Placemark></kml>", true, 1);
    check("<kml xmlns='http://www.opengis.net/kml/2.2'><Placemark><description xmlns=''><![CDATA[<img src=bad>]]></description><x:description xmlns:x='urn:other'><![CDATA[<img src=bad>]]></x:description><description><![CDATA[<img src=good>]]></description></Placemark></kml>", true, 1);
    check("<kml xmlns='http://www.opengis.net/kml/2.2'><Placemark><ExtendedData><Placemark><description><![CDATA[<img src=bad>]]></description></Placemark></ExtendedData><description><![CDATA[<script><img src=bad></script><img src=good>]]></description></Placemark></kml>", true, 1);
    check("<kml xmlns='urn:other'/>", false, 0);
    check("<kml><Placemark><description><img src='bad'/></description></Placemark></kml>", false, 0);
    check("<kml><Placemark><description><![CDATA[<img src=partial>]]></description>", false, 0);
    check("<!DOCTYPE kml><kml/>", false, 0);
    check("<kml><x:Placemark/></kml>", false, 0);
    check("<kml xmlns:a='urn:same' xmlns:b='urn:same' a:x='1' b:x='2'/>", false, 0);
    check("<kml><Placemark><description>&bad;</description></Placemark></kml>", false, 0);
    check("<kml/><kml/>", false, 0);
    check("<kml><Placemark><Point><coordinates>1,2,</coordinates></Point></Placemark></kml>", false, 0);
    check("<kml xmlns:g='http://www.google.com/kml/ext/2.2'><Placemark><g:Track><g:coord>1 2</g:coord></g:Track></Placemark></kml>", false, 0);
    check("<kml><Placemark><Point><coordinates>NaN,2</coordinates></Point></Placemark></kml>", false, 0);
    check("<kml><Placemark><Point><coordinates>1e999,2</coordinates></Point></Placemark></kml>", false, 0);
    std::string deep = "<kml>";
    for (unsigned i = 0; i < 64; ++i) deep += "<Folder>";
    for (unsigned i = 0; i < 64; ++i) deep += "</Folder>";
    check(deep + "</kml>", false, 0);
    std::string over_points = "<kml><Placemark><LineString><coordinates>";
    for (unsigned i = 0; i < 100001; ++i) over_points += "1,2 ";
    check(over_points + "</coordinates></LineString></Placemark></kml>", false, 0);
    check("<kml><Placemark><description><![CDATA[" + std::string(20000, 'x') + "<img src=good>]]></description></Placemark></kml>", true, 1);
    Result invalid;
    assert(!parse("<kml><Placemark><Point><coordinates>181,2</coordinates></Point><description><![CDATA[<img src=x>]]></description></Placemark></kml>", 1, invalid));
    Result shadow;
    assert(parse("<kml xmlns='http://www.opengis.net/kml/2.2'><Placemark><Point xmlns=''><coordinates>1,2</coordinates></Point><description><![CDATA[<img src=x>]]></description></Placemark></kml>", 1, shadow));
    assert(!shadow.images[0].located);
    Result polygon;
    assert(parse("<kml><Placemark><MultiGeometry><Polygon><outerBoundaryIs><LinearRing><coordinates>1,2 2,3 3,4 1,2</coordinates></LinearRing></outerBoundaryIs></Polygon></MultiGeometry><description><![CDATA[<img src=x>]]></description></Placemark></kml>", 1, polygon));
    assert(polygon.images[0].located && std::abs(polygon.images[0].lon - 1) < 1e-8);
    Result segmented;
    assert(parse("<kml xmlns:g='http://www.google.com/kml/ext/2.2'><Placemark><MultiGeometry><LineString><coordinates>1,2 2,3</coordinates></LineString><LineString><coordinates>3,4 4,5</coordinates></LineString><g:Track><g:coord>5 6 0</g:coord><g:coord/><g:coord>6 7 0</g:coord><g:coord>7 8 0</g:coord></g:Track><g:Track><g:coord>8 9 0</g:coord></g:Track></MultiGeometry></Placemark></kml>", 1, segmented));
    assert(segmented.coordinates.size() == 8);
    const bool breaks[] = {true, false, true, false, true, true, false, true};
    for (size_t i = 0; i < 8; ++i) assert(segmented.coordinates[i].break_before == breaks[i]);
}
