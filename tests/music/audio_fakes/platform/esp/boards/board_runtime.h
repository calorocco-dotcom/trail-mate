#pragma once
#include "boards/tlora_pager/tlora_pager_board.h"
#include <cassert>
namespace platform::esp::boards
{
inline ::boards::tlora_pager::TLoRaPagerBoard output;
inline bool available = true;
struct AppContextInitHandles
{
    void* board = nullptr;
};
inline bool tryResolveAppContextInitHandles(AppContextInitHandles* handles)
{
    handles->board = available ? &output : nullptr;
    return available;
}
} // namespace platform::esp::boards
