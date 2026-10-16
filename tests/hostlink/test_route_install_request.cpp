#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/route_install_request.h"
#include <cassert>
#include <cstring>
using namespace hostlink::management;
int main()
{
    uint8_t raw[512]{}, id[16]{1}, hash[32];
    std::memset(hash, 0xab, sizeof(hash));
    auto encode = [&](uint64_t bytes, uint64_t generation, size_t id_size = 16)
    {
        geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
        assert(writer.array(4) && writer.binary({id, id_size}) && writer.binary({hash, 32}) &&
               writer.unsignedInteger(bytes) && writer.unsignedInteger(generation));
        return writer.size();
    };
    RouteInstallView view;
    size_t size = encode(8388608, UINT32_MAX - 1);
    assert(parseRouteInstallRequest({raw, size}, view) == Status::Ok);
    assert(view.bytes == 8388608 && view.generation == UINT32_MAX - 1);
    assert(view.transfer.data >= raw && view.transfer.data < raw + size);
    assert(view.hash.data >= raw && view.hash.data < raw + size);
    char path[78];
    std::memset(path, '!', sizeof(path));
    assert(routeInstallTarget(view.hash, path, 77));
    assert(std::strlen(path) == 76 && path[77] == '!');
    assert(std::strncmp(path, "/routes/abab", 12) == 0 && std::strcmp(path + 72, ".kml") == 0);
    assert(!routeInstallTarget(view.hash, path, 76) && path[0] == '\0');
    uint8_t aliased[160], saved[160];
    std::memset(aliased, 0xab, sizeof(aliased));
    std::memcpy(saved, aliased, sizeof(saved));
    for (size_t offset : {size_t(20), size_t(40), size_t(50)})
    {
        assert(!routeInstallTarget({aliased + 40, 32}, reinterpret_cast<char*>(aliased + offset), 77));
        assert(!std::memcmp(aliased, saved, sizeof(saved)));
    }
    assert(!routeInstallTarget({aliased + 40, 32}, reinterpret_cast<char*>(aliased + 40), 1));
    assert(!std::memcmp(aliased, saved, sizeof(saved)));
    for (const auto total : {uint64_t(0), uint64_t(8388609), UINT64_MAX})
    {
        size = encode(total, 7);
        assert(parseRouteInstallRequest({raw, size}, view) == Status::InvalidField && !view.transfer.data);
    }
    for (const auto generation : {uint64_t(0), uint64_t(UINT32_MAX), UINT64_MAX})
    {
        size = encode(1024, generation);
        assert(parseRouteInstallRequest({raw, size}, view) == Status::InvalidField);
    }
    size = encode(1024, 7, 15);
    assert(parseRouteInstallRequest({raw, size}, view) == Status::InvalidField);
    std::memset(id, 0, sizeof(id));
    size = encode(1024, 7);
    assert(parseRouteInstallRequest({raw, size}, view) == Status::InvalidField);
    id[0] = 1;
    size = encode(1024, 7);
    for (size_t prefix = 0; prefix < size; ++prefix)
        assert(parseRouteInstallRequest({raw, prefix}, view) == Status::InvalidField);
    raw[size] = 0;
    assert(parseRouteInstallRequest({raw, size + 1}, view) == Status::InvalidField);
    assert(parseRouteInstallRequest({raw, 449}, view) == Status::InvalidField);
}
