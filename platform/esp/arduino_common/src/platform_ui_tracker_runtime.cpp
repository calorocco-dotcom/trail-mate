#include "platform/ui/tracker_runtime.h"

#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <atomic>
#include <cstring>
#include <strings.h>

namespace platform::ui::tracker
{
namespace
{

enum class TrackerCommand : uint8_t
{
    Start,
    Stop,
};

QueueHandle_t s_command_queue = nullptr;
TaskHandle_t s_worker_task = nullptr;
std::atomic_bool s_desired_recording{false};
std::atomic_bool s_start_pending{false};
std::atomic_bool s_stop_pending{false};

void tracker_worker_task(void*)
{
    TrackerCommand command = TrackerCommand::Start;
    while (true)
    {
        if (xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdPASS)
        {
            continue;
        }

        switch (command)
        {
        case TrackerCommand::Start:
        {
            s_start_pending.store(true);
            s_stop_pending.store(false);
            const bool ok = ::gps::TrackRecorder::getInstance().start();
            s_desired_recording.store(ok);
            s_start_pending.store(false);
            break;
        }
        case TrackerCommand::Stop:
            s_stop_pending.store(true);
            s_start_pending.store(false);
            ::gps::TrackRecorder::getInstance().stop();
            s_desired_recording.store(false);
            s_stop_pending.store(false);
            break;
        }
    }
}

bool ensure_worker()
{
    if (s_command_queue == nullptr)
    {
        s_command_queue = xQueueCreate(4, sizeof(TrackerCommand));
        if (s_command_queue == nullptr)
        {
            return false;
        }
    }
    if (s_worker_task == nullptr)
    {
        BaseType_t ok = xTaskCreate(tracker_worker_task,
                                    "tracker_io",
                                    4096,
                                    nullptr,
                                    1,
                                    &s_worker_task);
        if (ok != pdPASS)
        {
            s_worker_task = nullptr;
            return false;
        }
    }
    return true;
}

bool enqueue_command(TrackerCommand command)
{
    if (!ensure_worker())
    {
        return false;
    }
    xQueueReset(s_command_queue);
    return xQueueSend(s_command_queue, &command, 0) == pdPASS;
}

} // namespace

bool is_supported()
{
    return true;
}

bool is_recording()
{
    return s_desired_recording.load() ||
           s_start_pending.load() ||
           (!s_stop_pending.load() && ::gps::TrackRecorder::getInstance().isRecording());
}

bool start_recording()
{
    s_desired_recording.store(true);
    s_start_pending.store(true);
    s_stop_pending.store(false);
    if (enqueue_command(TrackerCommand::Start))
    {
        return true;
    }
    s_start_pending.store(false);
    s_desired_recording.store(::gps::TrackRecorder::getInstance().isRecording());
    return false;
}

void stop_recording()
{
    s_desired_recording.store(false);
    s_stop_pending.store(true);
    s_start_pending.store(false);
    if (!enqueue_command(TrackerCommand::Stop))
    {
        s_stop_pending.store(false);
    }
}

bool current_path(std::string& out_path)
{
    auto& recorder = ::gps::TrackRecorder::getInstance();
    if (!recorder.tryAcquireRead(false))
    {
        out_path.clear();
        return false;
    }
    const String& path = recorder.currentPath();
    if (path.isEmpty())
    {
        out_path.clear();
        recorder.releaseRead();
        return false;
    }
    out_path = path.c_str();
    recorder.releaseRead();
    return true;
}

bool list_tracks(std::vector<std::string>& out_tracks, std::size_t max_count)
{
    out_tracks.clear();
    if (max_count == 0)
    {
        return false;
    }

    std::vector<String> names(max_count);
    const std::size_t count = ::gps::TrackRecorder::getInstance().listTracks(names.data(), max_count);
    out_tracks.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        out_tracks.emplace_back(names[index].c_str());
    }
    return !out_tracks.empty();
}

void poll()
{
    (void)ensure_worker();
    ::gps::TrackRecorder::getInstance().applyPendingSettings();
    if (!s_start_pending.load() && !s_stop_pending.load())
    {
        s_desired_recording.store(::gps::TrackRecorder::getInstance().isRecording());
    }
}

bool remove_track(const std::string& path)
{
    if (!platform::ui::device::sd_ready() || path.empty())
    {
        return false;
    }
    return ::gps::TrackRecorder::getInstance().removeTrack(path.c_str());
}

const char* track_dir()
{
    return ::gps::TrackRecorder::kTrackDir;
}

bool try_acquire_snapshot_read()
{
    if (s_start_pending.load() || s_stop_pending.load()) return false;
    auto& recorder = ::gps::TrackRecorder::getInstance();
    if (!recorder.tryAcquireRead(true)) return false;
    if (s_start_pending.load() || s_stop_pending.load())
    {
        recorder.releaseRead();
        return false;
    }
    return true;
}

bool try_acquire_catalog_read()
{
    return ::gps::TrackRecorder::getInstance().tryAcquireRead(false);
}

void release_snapshot_read()
{
    ::gps::TrackRecorder::getInstance().releaseRead();
}

bool is_recording_filename(const char* filename)
{
    // Caller holds the catalog/snapshot mutex, so the borrowed path is stable.
    auto& recorder = ::gps::TrackRecorder::getInstance();
    const String& path = recorder.currentPath();
    return filename && recorder.isRecording() && !std::strncmp(path.c_str(), "/trackers/", 10) &&
           !strcasecmp(path.c_str() + 10, filename);
}

void set_auto_recording(bool enabled)
{
    ::gps::TrackRecorder::getInstance().setAutoRecording(enabled);
}

void set_interval_seconds(uint32_t seconds)
{
    ::gps::TrackRecorder::getInstance().setIntervalSeconds(seconds);
}

void set_distance_only(bool enabled)
{
    ::gps::TrackRecorder::getInstance().setDistanceOnly(enabled);
}

void set_format(Format format)
{
    ::gps::TrackRecorder::getInstance().setFormat(static_cast<::gps::TrackFormat>(format));
}

} // namespace platform::ui::tracker
