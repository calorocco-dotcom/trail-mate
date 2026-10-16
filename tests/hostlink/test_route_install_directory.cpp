#include "hostlink/route_install_directory.h"
#include <cassert>
#include <cstring>
struct Port
{
    uint32_t media = 1;
    bool exists = false, available = true, external = false, failure = false, swap = false;
    unsigned creates = 0;
    uint32_t session() const { return media; }
    bool ready() const { return available; }
    bool externalOwner() const { return external; }
    bool directory(const char* path, uint32_t expected) const
    {
        assert(!std::strcmp(path, "/routes"));
        return expected == media && exists;
    }
    bool mkdir(const char* path, uint32_t expected)
    {
        assert(!std::strcmp(path, "/routes"));
        if (swap) ++media;
        if (expected != media || failure) return false;
        ++creates;
        exists = true;
        return true;
    }
};
int main()
{
    using hostlink::management::ensureRouteInstallDirectory;
    using hostlink::management::Status;
    Port fresh;
    assert(ensureRouteInstallDirectory(fresh, 1) == Status::Ok && fresh.creates == 1 && fresh.exists);
    assert(ensureRouteInstallDirectory(fresh, 1) == Status::Ok && fresh.creates == 1);
    Port stale;
    assert(ensureRouteInstallDirectory(stale, 2) == Status::StorageUnavailable && !stale.creates);
    Port swapped;
    swapped.swap = true;
    assert(ensureRouteInstallDirectory(swapped, 1) == Status::StorageUnavailable && !swapped.creates && !swapped.exists);
    Port failed;
    failed.failure = true;
    assert(ensureRouteInstallDirectory(failed, 1) == Status::StorageUnavailable && !failed.creates);
    Port external;
    external.external = true;
    assert(ensureRouteInstallDirectory(external, 1) == Status::StorageUnavailable && !external.creates);
}
