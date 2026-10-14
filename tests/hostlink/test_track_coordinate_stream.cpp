#include "track_sharing/coordinate_stream.h"
#include <cassert>
#include <string>
using namespace track_sharing;
int main()
{
    const std::string text = "179,1,4 -179,-2,6\n";
    for (size_t split = 0; split <= text.size(); ++split)
    {
        GeometryBounds bounds;
        CoordinateStream stream(bounds, CoordinateText::KmlTuples);
        assert(stream.feed(text.data(), split) && stream.feed(text.data() + split, text.size() - split) && stream.finish());
        GeometryExtent result;
        assert(bounds.finish(result) && result.points == 2 && result.west == 1790000000 && result.east == -1790000000);
    }
    GeometryBounds gx;
    CoordinateStream stream(gx, CoordinateText::GxCoordinate);
    const std::string coordinate = "-179 -2 6";
    for (char character : coordinate) assert(stream.feed(&character, 1));
    assert(stream.finish());
    GeometryExtent result;
    assert(gx.finish(result) && result.points == 1);
    for (const std::string invalid : {"1,,2", "1,2,", "181,2", "1 2", "1e+,2", "nan,2", "1e999,2"})
    {
        GeometryBounds bounds;
        CoordinateStream parser(bounds, CoordinateText::KmlTuples);
        const bool valid = parser.feed(invalid.data(), invalid.size()) && parser.finish();
        assert(!valid && !bounds.finish(result));
    }
    GeometryBounds rounded;
    CoordinateStream parser(rounded, CoordinateText::KmlTuples);
    const std::string half = "-0.00000005,0";
    assert(parser.feed(half.data(), half.size()) && parser.finish() && rounded.finish(result) && result.west == 0);
}
