#include <array>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <limits>
#define ESP_LOGW(...) ((void)0)
constexpr int ESP_OK = 0;
constexpr uint8_t kRegControl1 = 0, kRegSeconds = 2;
constexpr uint8_t kControl1Stop = 0x20, kSecondsVl = 0x80, kMonthCentury = 0x80;
std::array<uint8_t, 9> regs{0, 0, 0x01, 0x02, 0x03, 0x15, 0x04, 0x10, 0x26};
bool transfer_ok = true;
struct ScopedRtcDevice
{
    bool ok() { return true; }
    int read(uint8_t reg, uint8_t* out, size_t size)
    {
        if (!transfer_ok) return -1;
        for (size_t i = 0; i < size; ++i) out[i] = regs[reg + i];
        return 0;
    }
    int read8(uint8_t reg, uint8_t* out) { return read(reg, out, 1); }
};
#include "rtc_actual.inc"
int main()
{
    std::time_t epoch = 0;
    assert(read_rtc_epoch(&epoch));
    assert(epoch == datetime_to_epoch_utc(2026, 10, 15, 3, 2, 1));
    regs[0] = kControl1Stop;
    assert(!read_rtc_epoch(&epoch));
    regs[0] = 0;
    regs[2] = kSecondsVl;
    assert(!read_rtc_epoch(&epoch));
    // 0x1A previously became a plausible 20 seconds despite invalid BCD.
    regs[2] = 0x1A;
    assert(!read_rtc_epoch(&epoch));
    regs[2] = 0x01;
    regs[5] = 0x30;
    regs[7] = 0x02;
    assert(!read_rtc_epoch(&epoch));
    transfer_ok = false;
    assert(!read_rtc_epoch(&epoch));
    std::cout << "P4 RTC stopped, low voltage, malformed BCD and calendar checks passed\n";
}
