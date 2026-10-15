#include "tmap/administration.h"
#include "tmap/integer_route.h"
#include "tmap/profile_job.h"
#include "tmap/resource_budget.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                 \
    do                                                           \
    {                                                            \
        if (!(x))                                                \
        {                                                        \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); \
            std::exit(1);                                        \
        }                                                        \
    } while (false)
class NoMemory final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override { return true; }
    void* allocate(size_t, size_t, tmap::MemoryRegion region) override
    {
        CHECK(region == tmap::MemoryRegion::External);
        ++requests;
        return nullptr;
    }
    void release(void*, size_t, size_t, tmap::MemoryRegion) override { std::abort(); }
    size_t requests = 0;
};
class EmptyFile final : public tmap::RandomAccessFile
{
  public:
    uint64_t size() const override { return 0; }
    tmap::Status readAt(uint64_t, uint8_t*, size_t) override { return tmap::Status::Missing; }
};
int main()
{
    tmap::ResourceOptions options;
    options.profile = true;
    options.geometry_points = 2;
    tmap::ResourceBudget budget;
    CHECK(tmap::resourceBudget(options, budget) == tmap::Result::Ok);
    CHECK(budget.total_bytes >= sizeof(tmap::PackageWorkspace));
    NoMemory provider;
    EmptyFile file;
    tmap::ProfileJob job(provider);
    const tmap::GeoPoint points[] = {{0, 0}, {1000, 1000}};
    CHECK(job.begin(file, points, 2) == tmap::Result::BudgetExceeded);
    CHECK(provider.requests == 1 && job.reservedBytes() == 0);
    job.cancel();
    CHECK(job.step(1) == tmap::Result::Cancelled);
    std::puts("Released SDK: terrain/profile/routing/admin headers and external-memory failure contract PASS");
}
