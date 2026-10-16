#pragma once
#include <cstdint>
namespace ui::controls
{
enum class Control : uint8_t
{
    Wifi,
    Lora,
    Gps,
    Volume,
    Brightness
};
enum class SwitchState : uint8_t
{
    Off,
    On,
    Changing,
    Unavailable,
    Failed
};
struct Snapshot
{
    SwitchState wifi = SwitchState::Unavailable, lora = SwitchState::Unavailable, gps = SwitchState::Unavailable;
    bool wifi_enabled = false, lora_enabled = false, gps_enabled = false;
    bool volume_supported = false, brightness_supported = false;
    uint8_t volume = 0, brightness = 50;
    char message[96]{};
};
class IQuickControls
{
  public:
    virtual ~IQuickControls() = default;
    // Read a bounded runtime projection; never starts a service or scans hardware.
    virtual bool snapshot(Snapshot& out) = 0;
    // Values: switches 0/1, volume 0..100, brightness 10..100. Implementations coalesce writes.
    virtual bool set(Control control, uint8_t value) = 0;
};
} // namespace ui::controls
