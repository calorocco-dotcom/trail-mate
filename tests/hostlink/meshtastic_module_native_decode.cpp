#include "meshtastic/localonly.pb.h"
#include <iostream>
#include <iterator>
#include <pb_decode.h>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    const std::vector<uint8_t> input(std::istreambuf_iterator<char>(std::cin), {});
    if (input.size() > meshtastic_LocalModuleConfig_size) return 1;
    meshtastic_LocalModuleConfig module = meshtastic_LocalModuleConfig_init_zero;
    auto stream = pb_istream_from_buffer(input.data(), input.size());
    if (!pb_decode(&stream, meshtastic_LocalModuleConfig_fields, &module))
    {
        std::cerr << PB_GET_ERROR(&stream);
        return 2;
    }
    // Only synthetic fixture flags/counts. Never print credentials or raw data.
    std::cout << "{\"version\":" << module.version
              << ",\"mqtt\":" << (module.has_mqtt && module.mqtt.enabled ? 1 : 0)
              << ",\"precision\":" << (module.has_mqtt && module.mqtt.has_map_report_settings ? module.mqtt.map_report_settings.position_precision : 0)
              << ",\"threshold\":" << (module.has_paxcounter ? module.paxcounter.wifi_threshold : 0)
              << ",\"rows\":" << (module.has_remote_hardware ? module.remote_hardware.available_pins_count : 0)
              << ",\"gpio\":" << (module.has_remote_hardware && module.remote_hardware.available_pins_count ? unsigned(module.remote_hardware.available_pins[0].gpio_pin) : 0)
              << "}";
}
