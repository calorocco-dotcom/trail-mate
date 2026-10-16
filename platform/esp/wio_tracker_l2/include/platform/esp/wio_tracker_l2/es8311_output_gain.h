#pragma once
#include <cstdint>

namespace boards::wio_tracker_l2::audio
{
inline constexpr uint8_t kDacVolumeRegister = 0x32;
inline constexpr uint8_t kDacUnityGain = 0xBF;

// ES8311 User Guide, section 11.3: 0xBF is 0 dB; 0xFF is +32 dB.
// The codec library's logarithmic percentage scale includes positive gain.
// User volume already scales PCM, so the fixed codec stage must not boost it.
// Callers hold the shared I2C lock and keep the amplifier disabled throughout.
template <typename WriteRegister, typename ReadRegister>
bool configure_unity_gain(WriteRegister write, ReadRegister read)
{
    uint8_t observed = 0;
    return write(kDacVolumeRegister, kDacUnityGain) && read(kDacVolumeRegister, observed) && observed == kDacUnityGain;
}
} // namespace boards::wio_tracker_l2::audio
