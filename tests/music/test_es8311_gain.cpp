#include "platform/esp/wio_tracker_l2/es8311_output_gain.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    using namespace boards::wio_tracker_l2::audio;
    // Reproduce the installed driver's setVolume(75) mapping against the
    // datasheet's 0.5 dB register steps: a 45% full-scale song clips at +17.5 dB.
    const auto old_register = int(255 * std::log10(9 * 0.75 + 1));
    const auto old_gain = std::pow(10.0, (old_register - 191) * 0.5 / 20);
    assert(old_register == 0xE2 && 32767 * 0.45 * old_gain > 32767);
    uint8_t dac_register = old_register;
    unsigned writes = 0, reads = 0;
    bool write_ok = true, read_ok = true, corrupt_read = false;
    auto write = [&](uint8_t reg, uint8_t value)
    {
        ++writes;
        assert(reg == 0x32);
        if (!write_ok) return false;
        dac_register = value;
        return true;
    };
    auto read = [&](uint8_t reg, uint8_t& value)
    {
        ++reads;
        assert(reg == 0x32);
        value = corrupt_read ? 0xFF : dac_register;
        return read_ok;
    };
    assert(configure_unity_gain(write, read));
    assert(writes == 1 && reads == 1 && dac_register == 0xBF);
    assert(std::pow(10.0, (int(dac_register) - 191) * 0.5 / 20) == 1);
    write_ok = false;
    assert(!configure_unity_gain(write, read) && reads == 1);
    write_ok = true;
    read_ok = false;
    assert(!configure_unity_gain(write, read));
    read_ok = true;
    corrupt_read = true;
    assert(!configure_unity_gain(write, read));
    std::puts("ES8311 unity gain replaces the +17.5 dB boost; failed or mismatched register I/O is rejected.");
}
