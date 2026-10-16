#include "ui_map_runtime/navigation/archive_calculation.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

// Test-only host adapters. Production receives SD and PSRAM adapters instead.
class Disk final : public tmap::WritableRouteFile
{
  public:
    ~Disk() override { close(); }
    bool open(const char* path, const char* mode)
    {
        close();
        file = std::fopen(path, mode);
        if (!file || !seek(0, SEEK_END)) return false;
#ifdef _WIN32
        const auto end = _ftelli64(file);
#else
        const auto end = ftello(file);
#endif
        if (end < 0) return false;
        length = static_cast<uint64_t>(end);
        return true;
    }
    void close()
    {
        if (file) std::fclose(file);
        file = nullptr;
        length = 0;
    }
    bool flush() { return file && !std::fflush(file); }
    uint64_t size() const override { return length; }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t count) override
    {
        if (busy_every && ++calls % busy_every == 0) return tmap::Status::Busy;
        if (!file || offset > length || count > length - offset || !seek(offset)) return tmap::Status::Invalid;
        if (std::fread(output, 1, count, file) != count) return tmap::Status::IoError;
        bytes += count;
        return tmap::Status::Ok;
    }
    tmap::Status writeAt(uint64_t offset, const uint8_t* data, size_t count) override
    {
        if (busy_every && ++calls % busy_every == 0) return tmap::Status::Busy;
        if (!file || !seek(offset) || std::fwrite(data, 1, count, file) != count) return tmap::Status::IoError;
        length = std::max(length, offset + count);
        return tmap::Status::Ok;
    }
    size_t calls = 0, busy_every = 0;
    uint64_t bytes = 0;

  private:
    bool seek(uint64_t offset, int origin = SEEK_SET)
    {
#ifdef _WIN32
        return !_fseeki64(file, static_cast<int64_t>(offset), origin);
#else
        return !fseeko(file, static_cast<off_t>(offset), origin);
#endif
    }
    FILE* file = nullptr;
    uint64_t length = 0;
};
class External final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t bytes, size_t, tmap::MemoryRegion region) override
    {
        assert(region == tmap::MemoryRegion::External);
        ++requests;
        if (refuse) return nullptr;
        auto* output = std::malloc(bytes);
        assert(output);
        live[output] = bytes;
        used += bytes;
        peak = std::max(peak, used);
        return output;
    }
    void release(void* pointer, size_t bytes, size_t, tmap::MemoryRegion region) override
    {
        assert(region == tmap::MemoryRegion::External && live.at(pointer) == bytes);
        live.erase(pointer);
        used -= bytes;
        std::free(pointer);
    }
    bool refuse = false;
    size_t used = 0, peak = 0, requests = 0;
    std::map<void*, size_t> live;
};
static tmap::Result finish(navigation::ArchiveCalculation& job)
{
    auto result = tmap::Result::More;
    for (uint32_t step = 0; step < 30000000 && navigation::ArchiveCalculation::pending(result); ++step)
        result = job.step(64);
    assert(!navigation::ArchiveCalculation::pending(result));
    return result;
}
int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 4);
    const bool london = argc == 4;
    auto map = std::make_unique<Disk>();
    auto spool = std::make_unique<Disk>();
    auto output = std::make_unique<Disk>();
    assert(map->open(argv[1], "rb"));
    const std::string spool_path = std::string(argv[2]) + ".spool";
    External memory;
    auto job = std::make_unique<navigation::ArchiveCalculation>(memory);
    navigation::ArchiveCalculation::Limits limits;
    if (!london) limits.states = 256;
    const tmap::GeoPoint origin = london ? tmap::GeoPoint{515009000, -1245000} : tmap::GeoPoint{0, 0};
    const tmap::GeoPoint destination = london ? tmap::GeoPoint{514036000, -3377000} : tmap::GeoPoint{0, 20000};
    std::vector<uint8_t> reference;
    for (size_t busy : {size_t{0}, size_t{7}})
    {
        assert(spool->open(spool_path.c_str(), "w+b") && output->open(argv[2], "w+b"));
        map->busy_every = spool->busy_every = output->busy_every = busy;
        map->bytes = map->calls = spool->calls = output->calls = 0;
        memory.peak = 0;
        assert(job->begin(*map, *spool, *output, origin, destination, limits) == tmap::Result::More);
        assert(finish(*job) == tmap::Result::Ok);
        assert(job->phase() == navigation::ArchiveCalculation::Phase::Ready);
        assert(job->legCount() == (london ? 550 : 3));
        const auto search_peak = memory.peak, after_search = memory.used;
        const auto reads = map->bytes;
        assert(after_search < 20000); // Search labels, queue and cursor released.
        memory.peak = after_search;
        assert(job->beginExport() == tmap::Result::More);
        assert(finish(*job) == tmap::Result::Ok);
        assert(job->phase() == navigation::ArchiveCalculation::Phase::Complete);
        assert(job->reservedBytes() == 0 && memory.used == 0 && memory.live.empty());
        const auto export_peak = memory.peak;
        const auto archive_bytes = output->size();
        assert(london ? (job->lengthCm() > 2000000 && job->lengthCm() < 2200000) : job->lengthCm() > 30000);
        assert(output->flush());
        output->close();
        spool->close();
        map->close();
        assert(output->open(argv[2], "rb"));
        output->busy_every = 0;
        std::vector<uint8_t> current(static_cast<size_t>(output->size()));
        assert(output->readAt(0, current.data(), current.size()) == tmap::Status::Ok);
        if (reference.empty()) reference = current;
        else assert(reference == current);
        tmap::MemoryObject<tmap::RouteArchiveWorkspace> workspace(memory);
        tmap::MemoryObject<tmap::RouteArchiveReader> reader(memory);
        tmap::MemoryObject<tmap::RouteArchiveValidationCursor> validation(memory);
        assert(workspace.create() == tmap::Result::Ok && reader.create(*workspace.get()) == tmap::Result::Ok);
        assert(reader.get()->open(*output) == tmap::Result::Ok);
        assert(reader.get()->info().points == job->pointCount() && reader.get()->info().length_cm == job->lengthCm());
        assert(validation.create() == tmap::Result::Ok);
        auto result = reader.get()->beginValidation(*validation.get());
        while (navigation::ArchiveCalculation::pending(result)) result = reader.get()->validationStep(*validation.get(), 16);
        assert(result == tmap::Result::Ok);
        validation.reset();
        reader.reset();
        workspace.reset();
        assert(memory.used == 0);
        std::cout << "{\"route\":\"" << (london ? "london" : "junction") << "\",\"busy_every\":" << busy
                  << ",\"length_cm\":" << job->lengthCm() << ",\"legs\":" << job->legCount()
                  << ",\"points\":" << job->pointCount() << ",\"search_provider_peak\":" << search_peak
                  << ",\"after_search\":" << after_search << ",\"export_provider_peak\":" << export_peak
                  << ",\"map_search_read_bytes\":" << reads << ",\"archive_bytes\":" << archive_bytes
                  << ",\"live_after_complete\":" << memory.used << "}\n";
        job->reset();
        assert(map->open(argv[1], "rb"));
    }
    const auto failure_path = std::string(argv[2]) + ".failure";
    assert(spool->open(spool_path.c_str(), "w+b") && output->open(failure_path.c_str(), "w+b"));
    memory.refuse = true;
    assert(job->begin(*map, *spool, *output, origin, destination, limits) == tmap::Result::BudgetExceeded);
    assert(memory.used == 0);
    memory.refuse = false;
    assert(job->begin(*map, *spool, *output, origin, destination, limits) == tmap::Result::More);
    job->cancel();
    assert(job->phase() == navigation::ArchiveCalculation::Phase::Cancelled && memory.used == 0);
    assert(job->step(1) == tmap::Result::Cancelled);
    output->close();
    spool->close();
    std::remove(spool_path.c_str());
    std::remove(failure_path.c_str());
}
