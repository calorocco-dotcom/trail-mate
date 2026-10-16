#pragma once

#include <cstdint>

namespace platform::ui::orientation
{

struct HeadingState
{
    bool available = false;
    bool sensor_ready = false;
    float heading_deg = 0.0f;
    int16_t raw_x = 0;
    int16_t raw_y = 0;
    int16_t raw_z = 0;
    uint32_t last_update_ms = 0;
};

enum class ScreenOrientation
{
    Landscape,
    Portrait,
};

enum class ScreenOrientationPolicy
{
    LandscapeLocked,
    SensorLandscapeOnly,
    SensorAuto,
};

struct ScreenOrientationState
{
    ScreenOrientationPolicy policy = ScreenOrientationPolicy::LandscapeLocked;
    ScreenOrientation active_orientation = ScreenOrientation::Landscape;
    ScreenOrientation sensor_requested_orientation = ScreenOrientation::Landscape;
    bool sensor_available = false;
    bool sensor_ready = false;
    bool portrait_supported = false;
    bool sensor_request_ignored = false;
};

HeadingState get_heading();
// Stable onboard magnetometer capability. This query never starts sampling or
// powers a sensor; it is independent of IMU screen rotation and heading readiness.
bool heading_supported();
// A view owns one balanced lease. Acquisition only schedules work; release
// stops the P4 sampling worker after the last consumer leaves.
bool acquire_heading();
void release_heading();
void ensure_heading_runtime();
ScreenOrientationState get_screen_orientation();
void ensure_screen_orientation_runtime();

} // namespace platform::ui::orientation
