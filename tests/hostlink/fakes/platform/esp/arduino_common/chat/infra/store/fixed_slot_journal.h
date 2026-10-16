#pragma once
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
namespace chat::storage::v2
{
inline bool recoverAtomicFile(const char* final, const char* temporary, const char*)
{
    using namespace platform::esp::arduino_common::storage;
    if (sd_exists(final)) return true;
    if (sd_exists(temporary))
    {
        fake_files[final] = fake_files[temporary];
        fake_files.erase(temporary);
    }
    return fake_available;
}
inline bool replaceFileAtomically(const char* temporary, const char* final, const char*)
{
    using namespace platform::esp::arduino_common::storage;
    if (!sd_exists(temporary) || fake_write_failure) return false;
    fake_files[final] = fake_files[temporary];
    fake_files.erase(temporary);
    return true;
}
} // namespace chat::storage::v2
