// Preview-only service fixture. Production widgets use the same IQuickControls port.
#include "music/service.h"
#include "ui/menu/menu_dashboard.h"
#include "ui_presentation/controls/quick_controls.h"
#include <algorithm>
#include <cstring>
music::IService* preview_music_service();
namespace
{
class Source final : public ui::controls::IQuickControls
{
    ui::controls::Snapshot state;

  public:
    Source()
    {
        state.wifi = ui::controls::SwitchState::Off;
        state.lora = state.gps = ui::controls::SwitchState::On;
        state.volume_supported = state.brightness_supported = true;
        state.volume = 35;
        state.brightness = 70;
    }
    bool snapshot(ui::controls::Snapshot& out) override
    {
        state.wifi_enabled = state.wifi == ui::controls::SwitchState::On;
        state.lora_enabled = state.lora == ui::controls::SwitchState::On;
        state.gps_enabled = state.gps == ui::controls::SwitchState::On;
        state.volume = preview_music_service()->volume();
        out = state;
        return true;
    }
    bool set(ui::controls::Control control, uint8_t value) override
    {
        using namespace ui::controls;
        switch (control)
        {
        case Control::Wifi:
            state.wifi = value ? SwitchState::On : SwitchState::Off;
            break;
        case Control::Lora:
            state.lora = value ? SwitchState::On : SwitchState::Off;
            break;
        case Control::Gps:
            state.gps = value ? SwitchState::On : SwitchState::Off;
            break;
        case Control::Volume:
        {
            music::Request request;
            request.action = music::Action::Volume;
            request.value = value;
            return preview_music_service()->send(request);
        }
        case Control::Brightness:
            state.brightness = std::clamp<unsigned>(value, 10, 100);
            break;
        }
        return true;
    }
} source;
} // namespace
void preview_bind_controls() { ui::menu::dashboard::bindControlsSource(&source); }
