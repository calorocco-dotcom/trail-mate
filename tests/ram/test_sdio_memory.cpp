#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_DMA = 2, MALLOC_CAP_8BIT = 4;
bool fail = false;
void* allocated = nullptr;
unsigned attempts = 0, frees = 0, destroyed = 0;
void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps)
{
    ++attempts;
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    assert(alignment == 64 && !allocated);
    if (fail) return nullptr;
    allocated = std::malloc(bytes + alignment);
    assert(allocated);
    return reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(allocated) + alignment - 1) & ~(alignment - 1));
}
void heap_caps_free(void*)
{
    assert(allocated);
    std::free(allocated);
    allocated = nullptr;
    ++frees;
}
struct Base
{
    virtual ~Base() = default;
};
struct alignas(64) SdioC6Transport : Base
{
#include "c6_sdio_allocator.inc"
    uint8_t scratch[2304]{};
    ~SdioC6Transport() override { ++destroyed; }
};
int main()
{
    fail = true;
    std::unique_ptr<Base> owner(new (std::nothrow) SdioC6Transport());
    assert(!owner && !allocated && attempts == 1 && !frees && !destroyed);
    fail = false;
    for (unsigned i = 0; i < 100; ++i)
    {
        owner.reset(new (std::nothrow) SdioC6Transport());
        assert(owner && allocated && reinterpret_cast<uintptr_t>(owner.get()) % 64 == 0);
        owner.reset(); // Verify matching delete through the polymorphic owner.
        assert(!allocated && frees == i + 1 && destroyed == i + 1);
    }
}
