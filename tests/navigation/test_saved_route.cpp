#include "ui_map_runtime/navigation/saved_route.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

class File final : public tmap::RandomAccessFile
{
  public:
    std::vector<uint8_t> bytes;
    uint64_t read_bytes = 0;
    size_t calls = 0;
    uint64_t size() const override { return bytes.size(); }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (++calls % 7 == 0) return tmap::Status::Busy;
        if (offset > bytes.size() || count > bytes.size() - offset) return tmap::Status::Invalid;
        std::copy_n(bytes.data() + offset, count, output);
        read_bytes += count;
        return tmap::Status::Ok;
    }
};
class Memory final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t count, size_t, tmap::MemoryRegion region) override
    {
        assert(region == tmap::MemoryRegion::External);
        if (refuse) return nullptr;
        void* data = std::malloc(count);
        assert(data);
        live += count;
        return data;
    }
    void release(void* pointer, size_t count, size_t, tmap::MemoryRegion region) override
    {
        assert(region == tmap::MemoryRegion::External && count <= live);
        live -= count;
        std::free(pointer);
    }
    bool refuse = false;
    size_t live = 0;
};
static void open(navigation::SavedRoute& route, File& file)
{
    auto result = route.begin(file);
    for (unsigned i = 0; i < 10000 && navigation::SavedRoute::pending(result); ++i) result = route.openStep(1);
    assert(result == tmap::Result::Ok && route.opened());
}
static void fix(navigation::SavedRoute& route, tmap::GeoPoint point, uint32_t now)
{
    auto result = route.beginFix(point, now);
    for (unsigned i = 0; i < 10000 && navigation::SavedRoute::pending(result); ++i) result = route.fixStep(1);
    assert(result == tmap::Result::Ok && !route.matching());
}
static bool count(void* context, const tmap::RouteArchiveSegment&)
{
    ++*static_cast<unsigned*>(context);
    return true;
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    File file;
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    file.bytes.assign(std::istreambuf_iterator<char>(input), {});
    Memory memory;
    auto route = std::make_unique<navigation::SavedRoute>(memory);
    open(*route, file);
    const auto reserved = route->reservedBytes();
    assert(reserved == memory.live && reserved < 2500);
    assert(route->destination().longitude == 20000 && route->destination().latitude == 0);
    // Opening at a loop/route endpoint must not infer arrival before following.
    fix(*route, {0, 20000}, 1000);
    fix(*route, {0, 20000}, 4000);
    fix(*route, {0, 20000}, 7000);
    assert(route->guidance(7000).status != navigation::GuidanceStatus::Arrived);
    fix(*route, {0, 0}, 8000);
    const auto at_start = route->guidance(8000);
    assert(at_start.status == navigation::GuidanceStatus::Following);
    assert(at_start.turn == navigation::Turn::Left && at_start.turn_m > 100 && at_start.turn_m < 120);
    assert(at_start.progress_m < 1 && at_start.remaining_m > 300);
    // View and GPS query cursors interleave correctly despite the shared page.
    const tmap::Bounds all{-1800000000, -900000000, 1800000000, 900000000};
    assert(route->beginView(all) == tmap::Result::Ok);
    auto observed = route->beginFix({10000, 10000}, 9000);
    auto view = tmap::Result::More;
    unsigned segments = 0;
    for (unsigned i = 0; i < 10000 && (navigation::SavedRoute::pending(observed) || navigation::SavedRoute::pending(view)); ++i)
    {
        if (navigation::SavedRoute::pending(observed)) observed = route->fixStep(1);
        if (navigation::SavedRoute::pending(view)) view = route->viewStep(1, count, &segments);
    }
    assert(observed == tmap::Result::Ok && view == tmap::Result::Ok && segments >= 3);
    assert(route->reservedBytes() == reserved);
    fix(*route, {100000, 100000}, 10000);
    assert(route->guidance(10000).status == navigation::GuidanceStatus::Locating);
    fix(*route, {100000, 100000}, 13000);
    fix(*route, {100000, 100000}, 16000);
    assert(route->guidance(16000).status == navigation::GuidanceStatus::OffRoute);
    assert(route->guidance(22000).status == navigation::GuidanceStatus::NoFix);
    fix(*route, {0, 20000}, 17000);
    fix(*route, {0, 20000}, 20000);
    fix(*route, {0, 20000}, 23000);
    assert(route->guidance(23000).status == navigation::GuidanceStatus::Arrived);
    assert(route->guidance(23000).remaining_m < 0.01);
    assert(route->beginFix({0, 0}, 24000, 6000) == tmap::Result::Invalid);
    assert(route->guidance(24000).status == navigation::GuidanceStatus::NoFix);
    assert(route->beginView(all, 1) == tmap::Result::Ok);
    view = tmap::Result::More;
    while (navigation::SavedRoute::pending(view)) view = route->viewStep(1, count, &segments);
    assert(view == tmap::Result::BudgetExceeded);
    route->close();
    assert(memory.live == 0 && !route->opened());
    memory.refuse = true;
    assert(route->begin(file) == tmap::Result::BudgetExceeded && memory.live == 0);
}
