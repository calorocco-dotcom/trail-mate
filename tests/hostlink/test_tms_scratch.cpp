#include "app/tms_config_codec.h"
#include <cassert>
#include <cstdlib>
#include <new>

namespace tms = app::tms;
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
static unsigned allocated = 0, freed = 0;
static bool fail_allocation = false;
static void* heap_caps_malloc(std::size_t size, unsigned capabilities)
{
    assert(size == sizeof(tms::LineScratch));
    assert(capabilities == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++allocated;
    return fail_allocation ? nullptr : std::malloc(size);
}
static void heap_caps_free(void* raw)
{
    assert(raw != nullptr);
    ++freed;
    std::free(raw);
}

// Compile the production lease verbatim. The fake allocator verifies ownership
// and failure paths without replacing the class with a behavioural model.
#include "tms_scratch_actual.inc"

int main()
{
    {
        ScopedLineScratch outer;
        assert(outer.ready());
        auto* original = s_line_scratch_storage;
        original->bytes[0] = 'X';
        {
            ScopedLineScratch validation;
            assert(validation.ready());
            assert(s_line_scratch_storage == original);
            assert(s_line_scratch_storage->bytes[0] == 'X');
            assert(allocated == 1);
        }
        assert(freed == 0 && s_line_scratch_storage == original);
    }
    assert(freed == 1 && s_line_scratch_storage == nullptr);
    fail_allocation = true;
    {
        ScopedLineScratch failed;
        assert(!failed.ready());
    }
    assert(allocated == 2 && freed == 1 && s_line_scratch_storage == nullptr);
    fail_allocation = false;
    auto early_return = []
    {
        ScopedLineScratch scratch;
        assert(scratch.ready());
        return;
    };
    early_return();
    assert(allocated == 3 && freed == 2 && s_line_scratch_storage == nullptr);
}
