#pragma once
namespace platform::ui::walkie
{
inline bool active = false;
inline bool ptt = false;
inline bool is_active() { return active; }
inline void set_ptt(bool pressed) { ptt = pressed; }
} // namespace platform::ui::walkie
