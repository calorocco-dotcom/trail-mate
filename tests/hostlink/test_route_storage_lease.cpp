#include "hostlink/route_storage_lease.h"
#include <cassert>
int main()
{
    namespace lease = hostlink::route_storage_lease;
    static_assert(sizeof(lease::held) <= 4, "Route lease must remain a tiny flag");
    assert(lease::acquire());
    {
        lease::Guard ui;
        assert(!ui.acquired());
    }
    assert(!lease::acquire()); // failed UI guard must not release task ownership
    lease::release();
    {
        lease::Guard ui;
        assert(ui.acquired());
        assert(!lease::acquire());
    }
    assert(lease::acquire());
    lease::release();
}
