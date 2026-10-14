#pragma once
#include "chat/ports/device_author.h"
#include "geocaching/protocol/cmp_reader.h"
#include "hostlink/management_envelope.h"
namespace hostlink::management
{
template <class Adapter>
Status processIdentityBackupExport(const Adapter& adapter, const EnvelopeView& request,
                                   uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!request.session || request.code != uint16_t(Operation::IdentityBackupExport) || !output || capacity < 448) return Status::InvalidField;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t generation = 0;
    if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        reader.position() + 1 != request.body_size) return Status::InvalidField;
    const uint8_t consent = request.body[reader.position()];
    if (consent == 0xc2) return Status::Unauthorized;
    if (consent != 0xc3) return Status::InvalidField;
    // Request metadata has been copied to scalars before output may overwrite it.
    switch (adapter.exportDeviceIdentityBackup(uint32_t(generation), output, capacity, written))
    {
    case chat::DeviceAuthorResult::Ok:
        return Status::Ok;
    case chat::DeviceAuthorResult::Unsupported:
        return Status::Unsupported;
    case chat::DeviceAuthorResult::StorageError:
        return Status::StorageUnavailable;
    case chat::DeviceAuthorResult::StaleIdentity:
        return Status::StaleIdentity;
    case chat::DeviceAuthorResult::InvalidInput:
        return Status::InvalidField;
    default:
        return Status::Busy;
    }
}
} // namespace hostlink::management
