#pragma once
#include <cstdint>
namespace chat
{
enum class DeviceIdentityRestoreResult : uint8_t
{
    CommittedRebootRequired,
    IndeterminateRebootRequired,
    Unavailable,
    StaleIdentity,
    InvalidInput,
    StorageError,
    Unsupported,
    StaleRadioIdentity,
};
}
