#pragma once
#include "chat/ports/device_identity_restore.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/identity_restore_request.h"
namespace hostlink::management
{
struct IdentityRestoreActivity
{
    bool configuration = false, geocaching = false, snapshot = false, upload = false,
         prepare = false, publication = false, network = false, restart = false;
    bool busy() const { return configuration || geocaching || snapshot || upload || prepare || publication || network || restart; }
};
struct IdentityRestoreWork
{
    IdentityRestoreView request;
    uint8_t crypto[352]{};
};
static_assert(sizeof(IdentityRestoreWork) <= 512, "Restore work must remain transient metadata and shared scratch");
// Caller owns quiescence admission, PSRAM work and command-buffer wiping.
// Response [0|1,nextGeneration]: 0 confirmed storage commit; 1 uncertain write.
// Both require reboot/reconnection and fresh proof; neither means live activation.
template <typename Adapter>
Status processIdentityRestore(Adapter& adapter, const EnvelopeView& input, const IdentityRestoreActivity& activity,
                              IdentityRestoreWork& work, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    struct Clear
    {
        IdentityRestoreWork& work;
        ~Clear()
        {
            volatile uint8_t* data = reinterpret_cast<volatile uint8_t*>(&work);
            for (size_t i = 0; i < sizeof(work); ++i) data[i] = 0;
        }
    } clear{work};
    if (!input.session || input.code != uint16_t(Operation::IdentityBackupRestore) || !output || capacity < 8) return Status::InvalidField;
    if (activity.busy()) return Status::Busy;
    const auto decoded = parseIdentityRestoreRequest({input.body, input.body_size}, work.request);
    if (decoded != Status::Ok) return decoded;
    const auto generation = work.request.expected_generation;
    const auto result = adapter.restoreDeviceIdentity(work.request, work.crypto, sizeof(work.crypto));
    using Result = chat::DeviceIdentityRestoreResult;
    if (result == Result::CommittedRebootRequired || result == Result::IndeterminateRebootRequired)
    {
        if (generation == UINT32_MAX) return Status::Internal;
        geocaching::protocol::CmpWriter writer(output, capacity);
        if (!writer.array(2) || !writer.unsignedInteger(result == Result::CommittedRebootRequired ? 0 : 1) || !writer.unsignedInteger(generation + 1)) return Status::Internal;
        written = writer.size();
        return Status::Ok;
    }
    return result == Result::StaleIdentity ? Status::StaleIdentity : result == Result::InvalidInput ? Status::InvalidField
                                                                 : result == Result::StorageError   ? Status::StorageUnavailable
                                                                 : result == Result::Unsupported    ? Status::Unsupported
                                                                                                    : Status::Busy;
}
} // namespace hostlink::management
