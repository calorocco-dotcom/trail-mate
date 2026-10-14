#include "track_sharing/track_datetime.h"
#include <cassert>
int main(int argc, char** argv)
{
    using track_sharing::validTrackDateTime;
    if (argc == 2) return validTrackDateTime(argv[1], true) ? 0 : 1;
    for (const char* text : {"2024-02-29T12:00:00Z", "2026-10-11T00:00:00.123+14:00", "2026-10-11T24:00:00.000", "-0001-02-29T00:00:00Z"}) assert(validTrackDateTime(text));
    for (const char* text : {"2026-02-29T00:00:00Z", "1900-02-29T00:00:00Z", "2026-10-11T24:00:00.1Z", "2026-10-11T00:00:00+14:01", "0000-01-01T00:00:00Z", "2026-10-11", "2026-10-11T00:00:60Z"}) assert(!validTrackDateTime(text));
    assert(validTrackDateTime(" ", true) && !validTrackDateTime(" "));
}
