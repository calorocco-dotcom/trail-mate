#include "hostlink/track_snapshot_selection.h"
#include <cassert>
using namespace hostlink::management;
int main()
{
    uint8_t id[16] = {1}, payload[256];
    const auto parse = [&](const char* name, uint64_t bytes, uint64_t format)
    {geocaching::protocol::CmpWriter writer(payload,sizeof(payload));assert(writer.array(5)&&writer.binary({id,16})&&writer.unsignedInteger(9)&&writer.text(name)&&writer.unsignedInteger(bytes)&&writer.unsignedInteger(format));TrackSnapshotSelection result;return decodeTrackSnapshotSelection({payload,writer.size()},result); };
    assert(parse("山径.GPX", 750, 1));
    assert(parse("a.kml", 8388608, 0));
    assert(!parse("a.kml", 0, 0));
    assert(!parse("a.kml", 8388609, 0));
    assert(!parse("../a.gpx", 10, 1));
    assert(!parse("ACTIVE.BIN", 10, 3));
    assert(!parse("a.gpx", 10, 2));
    id[0] = 0;
    assert(!parse("a.gpx", 10, 1));
}
