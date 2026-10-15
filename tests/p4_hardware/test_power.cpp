#include "boards/t_display_p4/board_profile.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
constexpr int ESP_OK = 0;
constexpr int kI2cTimeoutMs = 1000;
constexpr uint32_t kNoDataWarnMs = 5000;
constexpr uint8_t kExpanderRegOutput0 = 2, kExpanderRegOutput1 = 3;
constexpr uint8_t kExpanderRegPolarity0 = 4, kExpanderRegPolarity1 = 5;
constexpr uint8_t kExpanderRegConfig0 = 6, kExpanderRegConfig1 = 7;
struct ExpanderPinLocation
{
    bool valid;
    uint8_t port;
    uint8_t bit;
};
struct Operation
{
    uint8_t reg, value;
};
std::array<uint8_t, 8> registers{};
std::vector<Operation> operations;
int fail_read = -1;
int fail_write = -1;
void vTaskDelay(int) {}
int pdMS_TO_TICKS(int value) { return value; }
using uart_port_t = int;
std::string uart_command;
int uart_write_bytes(int, const char* data, int len)
{
    uart_command.assign(data, len);
    return len;
}
int uart_wait_tx_done(int, int) { return 0; }
int uart_flush_input(int) { return 0; }

class TDisplayP4Board
{
  public:
    bool expander_ready_ = false;
    uint8_t expander_output_port0_ = 0, expander_output_port1_ = 0;
    uint8_t expander_config_port0_ = 0, expander_config_port1_ = 0;
    struct SystemI2cDeviceConfig
    {
        const char* owner;
        int address;
        int hz;
    };
    struct ManagedSystemI2cGuard
    {
        ManagedSystemI2cGuard(TDisplayP4Board&, SystemI2cDeviceConfig, int) {}
        explicit operator bool() const { return true; }
        int transmitReceive(const uint8_t* reg, size_t, uint8_t* out, size_t, int)
        {
            if (*reg == fail_read) return -1;
            *out = registers[*reg];
            return ESP_OK;
        }
        int transmit(const uint8_t* data, size_t, int)
        {
            operations.push_back({data[0], data[1]});
            if (data[0] == fail_write) return -1;
            registers[data[0]] = data[1];
            return ESP_OK;
        }
    };
    static const auto& profile() { return boards::t_display_p4::kBoardProfile; }
    const auto& ioExpanderPins() { return profile().io_expander; }
    bool initializeI2cBuses() { return true; }
    bool initializeExpander();
    bool expanderPinMode(int, bool);
    bool expanderWrite(int, bool);
    bool expanderWriteActive(int, bool, bool);
    bool runColdBootPowerSequence();
};
struct esp_ldo_channel_config_t
{
    int chan_id, voltage_mv;
};
void* s_dsi_phy_ldo = nullptr;
int ldo_mv = 0, ldo_channel = 0;
int esp_ldo_acquire_channel(esp_ldo_channel_config_t* config, void** handle)
{
    ldo_mv = config->voltage_mv;
    ldo_channel = config->chan_id;
    *handle = &ldo_mv;
    return ESP_OK;
}
#include "power_actual.inc"

int main()
{
    registers.fill(0xFF); // XL9535 power-on output/configuration latches.
    TDisplayP4Board board;
    fail_read = kExpanderRegOutput1;
    assert(!board.initializeExpander() && !board.expander_ready_);
    fail_read = -1;
    assert(board.initializeExpander());
    operations.clear();
    assert(board.runColdBootPowerSequence());
    assert((registers[2] & 1) == 0);        // The shared 3V3 rail must stay enabled.
    assert((registers[3] & 1) != 0);        // IO10: USB PHY power enabled HIGH.
    assert((registers[2] & (1 << 5)) == 0); // Unowned Ethernet held in reset.
    assert((registers[2] & (1 << 2)) && (registers[2] & (1 << 3)));
    // No driver may be enabled until every initial latch was preloaded.
    assert(operations.size() > 8);
    for (size_t i = 0; i < 8; ++i) assert(operations[i].reg == 2 || operations[i].reg == 3);
    assert(operations[8].reg == kExpanderRegConfig0);

    const uint8_t before = board.expander_output_port0_;
    fail_write = kExpanderRegOutput0;
    assert(!board.expanderWrite(1, !(before & 2)));
    assert(board.expander_output_port0_ == before && !board.expander_ready_);
    fail_write = -1;
    assert(board.initializeExpander());
    const uint8_t config_before = board.expander_config_port0_;
    fail_write = kExpanderRegConfig0;
    assert(!board.expanderPinMode(7, true));
    assert(board.expander_config_port0_ == config_before && !board.expander_ready_);
    fail_write = -1;
    assert(board.initializeExpander());
    fail_write = kExpanderRegOutput0;
    assert(!board.runColdBootPowerSequence());
    assert(ensure_dsi_phy_power() && ldo_mv == 2500 && ldo_channel == 3);
    assert(should_try_p4_factory_baud(false, 0, false, 5000));
    assert(!should_try_p4_factory_baud(false, 0, false, 4999));
    assert(!should_try_p4_factory_baud(true, 0, false, 10000));
    assert(!should_try_p4_factory_baud(false, 1, false, 10000));
    assert(!should_try_p4_factory_baud(false, 0, true, 10000));
    assert(configure_l76k_update_rate(1, 9600));
    assert(uart_command == "$PCAS02,1000*2E\r\n");
    assert(configure_l76k_update_rate(1, 115200));
    assert(uart_command == "$PCAS02,200*1D\r\n");
    std::cout << "P4 rails, reset ordering, transfer failures and GNSS policy passed\n";
}
