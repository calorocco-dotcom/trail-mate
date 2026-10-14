#pragma once
#include "chat/ports/device_identity_restore.h"
#include "hostlink/identity_restore_request.h"
#include "hostlink/radio_identity_backup.h"
namespace chat
{
struct IMeshAdapter
{
    unsigned calls = 0;
    DeviceIdentityRestoreResult result = DeviceIdentityRestoreResult::CommittedRebootRequired;
    DeviceIdentityRestoreResult restoreDeviceIdentity(const hostlink::management::IdentityRestoreView&, uint8_t*, size_t)
    {
        ++calls;
        return result;
    }
    DeviceIdentityRestoreResult restoreRadioIdentity(const hostlink::management::RadioIdentityRestoreView&, uint8_t*, size_t)
    {
        ++calls;
        return result;
    }
};
} // namespace chat
