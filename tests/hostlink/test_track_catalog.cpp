#include "platform/esp/idf_common/hostlink_track_catalog.h"
#include <cassert>
#include <cstdio>
static bool held = false;
namespace platform::ui::tracker
{
bool try_acquire_catalog_read()
{
    if (held) return false;
    held = true;
    return true;
}
void release_snapshot_read()
{
    assert(held);
    held = false;
}
bool is_recording_filename(const char* name) { return !std::strcmp(name, "live.GPX"); }
} // namespace platform::ui::tracker
static uint8_t request_bytes[64], output[471];
int main()
{
    using namespace hostlink::management;
    namespace sd = platform::esp::arduino_common::storage;
    sd::rows = {{"folder", true}, {"active.bin"}, {"notes.txt"}, {"live.GPX", false, 900}, {"山径.csv", false, 1200}};
    const auto request = [&](bool first, uint64_t media, uint64_t position)
    {geocaching::protocol::CmpWriter writer(request_bytes,sizeof(request_bytes));assert(writer.array(1));if(first)assert(writer.nil());else assert(writer.array(2)&&writer.unsignedInteger(media)&&writer.unsignedInteger(position));EnvelopeView view;view.body=request_bytes;view.body_size=writer.size();return view; };
    size_t written = 0;
    assert(readTrackCatalog(request(true, 0, 0), output, sizeof(output), written) == Status::Ok && !held && sd::scans == 4);
    for (size_t i = 0; i < written; ++i) std::printf("%02x", output[i]);
    std::printf("\n");
    geocaching::protocol::CmpReader reader({output, written});
    size_t fields = 0;
    uint64_t value = 0;
    std::string_view name;
    assert(reader.array(fields, 3) && fields == 3 && reader.unsignedInteger(value) && value == 1 && reader.unsignedInteger(value) && value == 128 && reader.array(fields, 4) && fields == 4 && reader.text(name, 127) && name == "live.GPX" && reader.unsignedInteger(value) && value == 1 && reader.unsignedInteger(value) && value == 900 && reader.unsignedInteger(value) && value == 1 && reader.finished());
    assert(readTrackCatalog(request(false, 1, 128), output, sizeof(output), written) == Status::Ok && !held);
    assert(readTrackCatalog(request(false, 1, 160), output, sizeof(output), written) == Status::Ok && !held);
    geocaching::protocol::CmpReader end({output, written});
    assert(end.array(fields, 3) && end.unsignedInteger(value) && end.nil() && end.nil() && end.finished());
    assert(readTrackCatalog(request(false, 2, 0), output, sizeof(output), written) == Status::Conflict && !held);
    assert(readTrackCatalog(request(false, 1, 1), output, sizeof(output), written) == Status::InvalidField);
    sd::busy = true;
    assert(readTrackCatalog(request(true, 0, 0), output, sizeof(output), written) == Status::Busy && !held);
    sd::busy = false;
    sd::unavailable = true;
    assert(readTrackCatalog(request(true, 0, 0), output, sizeof(output), written) == Status::StorageUnavailable && !held);
    sd::unavailable = false;
    sd::rows = {{"one.txt"}, {"two.txt"}, {"three.txt"}, {"four.txt"}, {"five.bin"}};
    sd::scans = 0;
    assert(readTrackCatalog(request(true, 0, 0), output, sizeof(output), written) == Status::Ok && sd::scans == 4);
    geocaching::protocol::CmpReader skipped({output, written});
    assert(skipped.array(fields, 3) && skipped.unsignedInteger(value) && skipped.unsignedInteger(value) && value == 128 && skipped.nil() && skipped.finished());
    assert(trackCatalogNameValid("山径.csv") && !trackCatalogNameValid("../bad.gpx") && !trackCatalogNameValid(std::string_view("\xc0\x80.gpx", 6)));
    sd::rows = {{"ACTIVE.BIN"}, {"Active.Bin"}, {"active.bin"}, {"walk.bin", false, 42}};
    sd::scans = 0;
    assert(readTrackCatalog(request(true, 0, 0), output, sizeof(output), written) == Status::Ok && !held && sd::scans == 4);
    geocaching::protocol::CmpReader mixed({output, written});
    assert(mixed.array(fields, 3) && mixed.unsignedInteger(value) && mixed.unsignedInteger(value) && value == 128 && mixed.array(fields, 4) && mixed.text(name, 127) && name == "walk.bin" && mixed.unsignedInteger(value) && value == 3 && mixed.unsignedInteger(value) && value == 42 && mixed.unsignedInteger(value) && value == 0 && mixed.finished());
    return 0;
}
