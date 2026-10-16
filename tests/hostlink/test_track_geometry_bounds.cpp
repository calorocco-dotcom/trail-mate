#include "track_sharing/geometry_bounds.h"
#include <cassert>
int main()
{
    track_sharing::GeometryBounds bounds;
    track_sharing::GeometryExtent output;
    assert(!bounds.finish(output));
    assert(bounds.add(1790000000, 10000000) && bounds.add(-1790000000, -20000000) && bounds.finish(output));
    assert(output.west == 1790000000 && output.east == -1790000000 && output.south == -20000000 && output.north == 10000000 && output.points == 2);
    track_sharing::GeometryBounds edge;
    assert(edge.add(1800000000, 900000000) && edge.finish(output) && output.west == -1800000000);
    track_sharing::GeometryBounds limit;
    for (unsigned i = 0; i < 100000; ++i) assert(limit.add(0, 0));
    assert(limit.finish(output) && output.points == 100000);
    assert(!limit.add(0, 0) && !limit.finish(output));
}
