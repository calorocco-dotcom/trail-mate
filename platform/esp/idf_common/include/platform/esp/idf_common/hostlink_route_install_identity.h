#pragma once
#include "platform/esp/idf_common/hostlink_route_install_owner.h"
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
namespace hostlink::management::route_install_runtime
{
template <class Adapter, class Guard, class Action>
Status withCurrentRouteIdentity(RouteInstallOwner& owner, Adapter& adapter, uint8_t* scratch, size_t capacity, Guard&& policy, Action&& action)
{
    if (!scratch || capacity < 448 || capacity > 512) return Status::InvalidField;
    const auto a = reinterpret_cast<uintptr_t>(&owner), b = reinterpret_cast<uintptr_t>(scratch);
    if (a <= b ? b - a < sizeof(owner) : a - b < capacity) return Status::InvalidField;
    const auto live = [&]
    {
        uint8_t hash[32];
        uint32_t generation = 0;
        const auto result = upload_runtime::readUploadAuthor(adapter, scratch + capacity - 64, 64, hash, generation);
        if (result != Status::Ok) return result;
        if (!owner.identityMatches(generation, {hash, 32})) return Status::StaleIdentity;
        return policy();
    };
    return action(live);
}
template <class Adapter, class Guard>
Status startAuthenticatedRouteInstall(RouteInstallOwner& owner, Adapter& adapter, bool install, uint8_t* scratch, size_t capacity, Guard&& policy)
{
    return withCurrentRouteIdentity(owner, adapter, scratch, capacity, policy, [&](auto& live)
                                    { return owner.start(install, scratch, capacity, live); });
}
template <class Adapter, class Guard>
Status advanceAuthenticatedRouteInstall(RouteInstallOwner& owner, Adapter& adapter, uint8_t* scratch, size_t capacity, Guard&& policy)
{
    return withCurrentRouteIdentity(owner, adapter, scratch, capacity, policy, [&](auto& live)
                                    { return owner.advance(scratch, capacity, live); });
}
} // namespace hostlink::management::route_install_runtime
