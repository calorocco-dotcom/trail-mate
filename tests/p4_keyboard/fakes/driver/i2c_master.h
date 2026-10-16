#pragma once
#include "vendor.h"
constexpr int ESP_OK = 0;
inline int i2c_master_probe(cpp_bus_driver::HardwareI2c1* bus, uint16_t address, int timeout)
{
    assert(timeout == 5);
    return bus->Probe(address) ? ESP_OK : -1;
}
