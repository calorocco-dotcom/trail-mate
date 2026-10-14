#include "hostlink/route_install_storage_policy.h"
#include <cassert>
using namespace hostlink::management;
int main()
{
    RouteInstallStorageActivity activity;
    assert(routeInstallStorageAdmission(activity) == Status::Ok);
    for (unsigned index = 0; index < 6; ++index)
    {
        activity = {};
        switch (index)
        {
        case 0:
            activity.restart = true;
            break;
        case 1:
            activity.snapshot = true;
            break;
        case 2:
            activity.prepare = true;
            break;
        case 3:
            activity.publication = true;
            break;
        case 4:
            activity.configuration = true;
            break;
        default:
            activity.active_route = true;
            break;
        }
        assert(routeInstallStorageAdmission(activity) == Status::Busy);
    }
    for (uint16_t op : {uint16_t(0), uint16_t(1), uint16_t(2), uint16_t(3), uint16_t(4), uint16_t(16), uint16_t(48), uint16_t(85), uint16_t(95)}) assert(!routeInstallBlocksOperation(op));
    for (uint16_t op : {uint16_t(5), uint16_t(17), uint16_t(21), uint16_t(32), uint16_t(33), uint16_t(34), uint16_t(35), uint16_t(36), uint16_t(50), uint16_t(66), uint16_t(67), uint16_t(68), uint16_t(80), uint16_t(81), uint16_t(82), uint16_t(83), uint16_t(86), uint16_t(91), uint16_t(65535)}) assert(routeInstallBlocksOperation(op));
    uint8_t hash[32];
    std::memset(hash, 0xab, sizeof(hash));
    char path[96]{};
    assert(routeInstallTarget({hash, 32}, path, sizeof(path)) && routeInstallPathMatches({hash, 32}, path, sizeof(path)));
    path[76] = 'x';
    assert(!routeInstallPathMatches({hash, 32}, path, sizeof(path)));
    path[76] = '\0';
    path[9] = 'A';
    assert(!routeInstallPathMatches({hash, 32}, path, sizeof(path)));
    assert(!routeInstallPathMatches({hash, 32}, path, 76));
}
