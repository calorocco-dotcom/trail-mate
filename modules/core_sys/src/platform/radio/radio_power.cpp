#include "platform/radio/radio_power.h"
namespace platform::radio
{
PowerPolicy& power_policy()
{
    // Lock-free control word stays internal on ESP; bulk widget state stays in PSRAM.
    static PowerPolicy policy;
    return policy;
}
} // namespace platform::radio
