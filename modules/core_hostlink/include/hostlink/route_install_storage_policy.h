#pragma once
#include "hostlink/route_install_request.h"
namespace hostlink::management
{
struct RouteInstallStorageActivity
{
    bool restart = false, snapshot = false, prepare = false, publication = false, configuration = false, active_route = false;
};
static_assert(sizeof(RouteInstallStorageActivity) <= 8, "Route admission must remain scalar-only");
inline Status routeInstallStorageAdmission(const RouteInstallStorageActivity& activity)
{
    return activity.restart || activity.snapshot || activity.prepare || activity.publication || activity.configuration || activity.active_route ? Status::Busy : Status::Ok;
}
// Default-deny while an immutable route lease is held. Unknown future commands
// must be reviewed explicitly before they can run alongside installation.
constexpr bool routeInstallBlocksOperation(uint16_t operation)
{
    switch (static_cast<Operation>(operation))
    {
    case Operation::Capabilities:
    case Operation::AuthorInfo:
    case Operation::AuthorProve:
    case Operation::SessionClose:
    case Operation::IdentityBackupExport:
    case Operation::ConfigSchema:
    case Operation::JobGet:
    case Operation::RouteInstall:
    case Operation::RouteInstallStatus:
        return false;
    default:
        return true;
    }
}
// Compare the configured active route against a content-addressed target
// without constructing a string or allocating another pathname buffer.
inline bool routeInstallPathMatches(geocaching::ByteView hash, const char* path, size_t capacity)
{
    if (!hash.data || hash.size != 32 || !path || capacity < 77 || std::memcmp(path, "/routes/", 8)) return false;
    constexpr char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < 32; ++index)
        if (path[8 + index * 2] != digits[hash.data[index] >> 4] || path[9 + index * 2] != digits[hash.data[index] & 15]) return false;
    return !std::memcmp(path + 72, ".kml", 5);
}
} // namespace hostlink::management
