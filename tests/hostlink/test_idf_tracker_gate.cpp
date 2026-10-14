#include "platform/ui/tracker_access_gate.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <cstring>

static bool available = true, fail_open = false;
static unsigned headers = 0, footers = 0, samples = 0, removals = 0;
namespace platform::ui::gps
{
struct GpsState
{
    bool valid = true;
    double lat = 30, lng = 120;
};
GpsState get_data() { return {}; }
} // namespace platform::ui::gps
namespace platform::esp::idf_common::bsp_runtime
{
bool ensure_sdcard_ready() { return available; }
} // namespace platform::esp::idf_common::bsp_runtime
namespace platform::esp::arduino_common::storage
{
bool sd_remove(const char*)
{
    ++removals;
    return true;
}
} // namespace platform::esp::arduino_common::storage
namespace platform::ui::tracker
{
struct File
{
    bool opened = false;
    bool open(const char*, const char*)
    {
        opened = !fail_open;
        return opened;
    }
    void close() { opened = false; }
    bool is_open() const { return opened; }
};
struct TrackerRuntimeState
{
    bool recording = false, auto_recording = false, has_last_point = false, distance_only = false;
    int format = 0;
    uint32_t interval_seconds = 1;
    uint64_t last_sample_ms = 0;
    double last_lat = 0, last_lng = 0;
    File file;
    std::string current_rel_path;
};
static TrackerRuntimeState runtime;
constexpr double kDistanceOnlyThresholdMeters = 5.0;
static TrackerControlState tracker_control;
TrackerRuntimeState& state() { return runtime; }
bool ensure_track_dir() { return available; }
std::string make_track_path(int) { return "/trackers/test.gpx"; }
std::string full_path_for(const std::string& value) { return value; }
void write_header(TrackerRuntimeState&) { ++headers; }
void write_footer(TrackerRuntimeState&) { ++footers; }
void write_point(TrackerRuntimeState&, const platform::ui::gps::GpsState&, uint32_t) { ++samples; }
uint64_t now_ms()
{
    static uint64_t counter = 0;
    return counter += 2000;
}
uint32_t now_seconds() { return 1700000000; }
double distance_meters(double, double, double, double) { return 10; }
void poll();
#include "idf_tracker_gate_actual.inc"
} // namespace platform::ui::tracker
int main()
{
    using namespace platform::ui::tracker;
    assert(start_recording() && runtime.recording && headers == 1 && samples == 1);
    assert(!try_acquire_snapshot_read());
    stop_recording();
    assert(!runtime.recording && footers == 1);
    assert(try_acquire_snapshot_read());
    assert(!try_acquire_snapshot_read() && !start_recording() && !remove_track("/trackers/test.gpx"));
    runtime.auto_recording = true;
    poll();
    stop_recording();
    assert(!runtime.recording && samples == 1 && removals == 0);
    release_snapshot_read();
    assert(remove_track("/trackers/test.gpx") && removals == 1);
    poll();
    assert(!runtime.recording && !tracker_control.stop_requested.load());
    poll();
    assert(runtime.recording && headers == 2 && samples == 2);
    stop_recording();
    runtime.auto_recording = false;
    fail_open = true;
    assert(!start_recording());
    assert(try_acquire_snapshot_read());
    release_snapshot_read();
    assert(tracker_control.access.tryAcquire());
    assert(!try_acquire_snapshot_read() && !start_recording());
    tracker_control.access.release();
    fail_open = false;
    assert(start_recording() && runtime.recording);
    assert(tracker_control.access.tryAcquire());
    stop_recording();
    assert(runtime.recording && tracker_control.stop_requested.load());
    tracker_control.access.release();
    assert(!try_acquire_snapshot_read() && !start_recording());
    poll();
    assert(!runtime.recording && !tracker_control.stop_requested.load());
    assert(try_acquire_snapshot_read());
    release_snapshot_read();
    return 0;
}
