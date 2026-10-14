#pragma once
#include <cstdint>
inline int esp_efuse_mac_get_default(uint8_t* out)
{
    const uint8_t fixture[] = {2, 3, 4, 5, 6, 7};
    for (unsigned i = 0; i < 6; ++i) out[i] = fixture[i];
    return 0;
}
