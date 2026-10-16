#pragma once
#include "boards/t_display_p4/board_profile.h"
#include "hardware.h"
namespace boards::t_display_p4
{
class TDisplayP4Board
{
  public:
    static auto& instance()
    {
        static TDisplayP4Board board;
        return board;
    }
    static const auto& profile() { return kBoardProfile; }
    bool ensureExternal3v3Power() { return fake::power; }
    void* externalI2cHandle() { return this; }
    const auto& externalI2c() { return profile().ext_i2c; }
};
} // namespace boards::t_display_p4
