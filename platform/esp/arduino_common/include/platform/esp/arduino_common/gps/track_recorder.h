#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <Arduino.h>

#include <atomic>
#include <cstddef>

namespace gps
{
static_assert(sizeof(std::atomic_bool) == sizeof(bool), "Recorder flags must retain their byte budget");
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t), "Recorder interval must retain its word budget");
static_assert(sizeof(std::atomic<uint8_t>) == 1, "Deferred recorder settings require one byte each");

struct TrackPoint
{
    double lat = 0;
    double lon = 0;
    uint8_t satellites = 0;
    time_t timestamp = 0;
};

enum class TrackFormat : uint8_t
{
    GPX = 0,
    CSV = 1,
    Binary = 2
};

// Simple GPX recorder with append-only writes for SD stability.
class TrackRecorder
{
  public:
    static TrackRecorder& getInstance();

    bool start();
    void stop();
    bool isRecording() const { return recording_; }
    const String& currentPath() const { return current_path_; }
    bool restoreActiveSession();

    void setAutoRecording(bool enabled);
    void setIntervalSeconds(uint32_t seconds);
    void setDistanceOnly(bool enabled);
    void setFormat(TrackFormat format);
    void applyPendingSettings();

    // Append a single point if recording is active and SD is ready.
    void appendPoint(const TrackPoint& pt);
    void flushPending(bool force = false);

    // List GPX files under /trackers; returns count.
    size_t listTracks(String* out_names, size_t max_names) const;
    // Nonblocking; shares the recording mutex with writers and future snapshots.
    // Only a direct child of /trackers may be removed, never the live recording.
    bool removeTrack(const char* path);
    // Borrow the existing mutex; caller must release on the same task exactly
    // once. Catalog reads allow recording; snapshots require it to be stopped.
    bool tryAcquireRead(bool require_stopped);
    void releaseRead();

    static constexpr const char* kTrackDir = "/trackers";

  private:
    TrackRecorder() = default;
    TrackRecorder(const TrackRecorder&) = delete;
    TrackRecorder& operator=(const TrackRecorder&) = delete;

    bool ensureDir() const;
    String makeTrackPath() const;
    static String isoTime(time_t t);
    const char* formatExtension() const;
    void beginNewFile();
    void updateActiveStateLocked();
    bool writeActiveStateLocked() const;
    void clearActiveStateLocked() const;
    bool shouldAcceptPointLocked(const TrackPoint& pt, uint32_t now_ms) const;
    void markPointAcceptedLocked(const TrackPoint& pt, uint32_t now_ms);
    bool writePendingPointsLocked(bool force);
    void applyPendingSettingsLocked();
    void applyAutoRecordingLocked(bool enabled);
    void applyFormatLocked(TrackFormat format);

    static constexpr size_t kPendingPointCapacity = 16;

    SemaphoreHandle_t mutex_ = xSemaphoreCreateMutex();
    std::atomic_bool recording_{false};
    bool auto_recording_ = false;
    bool manual_recording_ = false;
    String current_path_{};
    bool last_point_valid_ = false;
    TrackPoint last_point_{};
    time_t last_point_time_ = 0;
    uint32_t last_point_ms_ = 0;
    std::atomic<uint32_t> min_interval_ms_{0};
    std::atomic_bool distance_only_{false};
    TrackFormat format_ = TrackFormat::GPX;
    // Latest requested settings survive a busy snapshot/writer. Load/store
    // only: no read/modify/write atomic operations on the PSRAM recorder.
    std::atomic<uint8_t> requested_format_{0xff};
    std::atomic<uint8_t> requested_auto_recording_{0xff};
    TrackPoint pending_points_[kPendingPointCapacity]{};
    size_t pending_point_count_ = 0;
    uint32_t last_flush_ms_ = 0;
};

} // namespace gps
