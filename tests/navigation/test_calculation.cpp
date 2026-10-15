#include "ui_map_runtime/navigation/route_calculation.h"
#include "ui_map_runtime/navigation/route_kml_writer.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

class Memory : public tmap::MemoryProvider
{
  public:
    std::map<void*, size_t> active;
    size_t bytes = 0, peak = 0;
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t size, size_t, tmap::MemoryRegion region) override
    {
        assert(region == tmap::MemoryRegion::External);
        auto* pointer = std::malloc(size);
        assert(pointer);
        active[pointer] = size;
        bytes += size;
        peak = std::max(peak, bytes);
        return pointer;
    }
    void release(void* pointer, size_t size, size_t, tmap::MemoryRegion region) override
    {
        assert(region == tmap::MemoryRegion::External && active.at(pointer) == size);
        active.erase(pointer);
        bytes -= size;
        std::free(pointer);
    }
};
class File : public tmap::RandomAccessFile
{
  public:
    std::vector<uint8_t> data;
    size_t calls = 0, busy_every = 0;
    uint64_t size() const override { return data.size(); }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (busy_every && ++calls % busy_every == 0) return tmap::Status::Busy;
        if (offset > data.size() || count > data.size() - offset) return tmap::Status::Invalid;
        std::copy_n(data.data() + offset, count, output);
        return tmap::Status::Ok;
    }
};
struct Geometry
{
    navigation::RouteKmlWriter writer;
    std::vector<tmap::GeoPoint> points;
    std::string kml;
};
static bool write(void* context, const char* bytes, size_t count)
{
    static_cast<Geometry*>(context)->kml.append(bytes, count);
    return true;
}
static bool point(void* context, tmap::GeoPoint value)
{
    auto& geometry = *static_cast<Geometry*>(context);
    geometry.points.push_back(value);
    return geometry.writer.point(value);
}
static tmap::Result finish(navigation::RouteCalculation& calculation)
{
    auto result = tmap::Result::More;
    for (unsigned step = 0; step < 100000 && navigation::RouteCalculation::pending(result); ++step)
        result = calculation.step(4);
    assert(!navigation::RouteCalculation::pending(result));
    return result;
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    File file;
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    file.data.assign(std::istreambuf_iterator<char>(input), {});
    Memory memory;
    navigation::RouteCalculation calculation(memory, tmap::MemoryPolicy::RequireExternal);
    assert(memory.active.empty());
    for (const size_t busy : {size_t{0}, size_t{7}})
    {
        file.busy_every = busy;
        assert(calculation.begin(file, {0, 0}, {0, 20000}, tmap::TravelMode::Foot, {}) == tmap::Result::More);
        assert(finish(calculation) == tmap::Result::Ok);
        assert(calculation.legCount() == 3 && calculation.costUnits() > 30000);
        Geometry output;
        assert(output.writer.begin(write, &output, "Origin & <gate>", {0, 0}, "Destination \"ridge\"", {0, 20000}));
        assert(calculation.beginGeometry(point, &output) == tmap::Result::More);
        assert(finish(calculation) == tmap::Result::Ok);
        assert(output.writer.finish());
        const auto& geometry = output.points;
        assert(geometry.size() >= 4 && geometry.front().latitude == 0 && geometry.front().longitude == 0);
        assert(geometry.back().latitude == 0 && geometry.back().longitude == 20000);
        assert(std::any_of(geometry.begin(), geometry.end(), [](const auto& p)
                           { return p.latitude == 10000; }));
        std::ofstream saved(std::string(argv[1]) + ".kml", std::ios::binary);
        saved << output.kml;
        assert(saved.good());
        calculation.reset();
        assert(memory.active.empty() && memory.bytes == 0);
    }
    assert(calculation.begin(file, {0, 20000}, {0, 0}, tmap::TravelMode::Foot, {}) == tmap::Result::More);
    assert(finish(calculation) == tmap::Result::NoPath);
    assert(memory.active.empty());
    assert(calculation.begin(file, {0, 0}, {0, 20000}, tmap::TravelMode::Foot, {}) == tmap::Result::More);
    for (unsigned i = 0; i < 10; ++i) calculation.step(1);
    calculation.cancel();
    assert(calculation.phase() == navigation::RouteCalculation::Phase::Cancelled && memory.active.empty());
    assert(memory.peak <= 180 * 1024);
}
