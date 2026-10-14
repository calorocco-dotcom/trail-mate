#pragma once
#include "hostlink/management_envelope.h"
namespace hostlink::management
{
// Port.mkdir must enforce expected_session while holding the filesystem lock.
// Only installation calls this; status queries never create directories.
template <class Port>
Status ensureRouteInstallDirectory(Port& port, uint32_t expected_session)
{
    const auto ready = [&]
    { return port.session() == expected_session && port.ready() && !port.externalOwner(); };
    if (!ready()) return Status::StorageUnavailable;
    if (port.directory("/routes", expected_session)) return ready() ? Status::Ok : Status::StorageUnavailable;
    if (!ready() || !port.mkdir("/routes", expected_session)) return Status::StorageUnavailable;
    return ready() && port.directory("/routes", expected_session) ? Status::Ok : Status::StorageUnavailable;
}
} // namespace hostlink::management
