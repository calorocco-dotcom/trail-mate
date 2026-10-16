#include "platform/radio/radio_power.h"
#include <cassert>
int main()
{
    platform::radio::PowerPolicy policy;
    assert(policy.enabled() && policy.status().applied);
    assert(policy.begin_exclusive());
    assert(!policy.request(false)); // A walkie/USB/probe owns the transition boundary.
    assert(!policy.begin_exclusive());
    assert(policy.status().exclusive);
    policy.end_exclusive();
    assert(policy.request(false));
    assert(!policy.enabled() && policy.status().applied); // No optimistic physical ACK.
    policy.fail();
    assert(policy.status().failed && policy.status().applied);
    assert(policy.request(false));
    assert(!policy.status().failed);
    policy.acknowledge(false);
    assert(!policy.status().applied);
    assert(policy.begin_exclusive()); // USB storage works with RF disabled.
    assert(!policy.request(true));
    policy.end_exclusive();
    assert(policy.request(true));
    policy.acknowledge(false); // A stale completion must not change a newer preference.
    assert(policy.enabled() && !policy.status().applied);
    policy.acknowledge(true);
    assert(policy.status().applied);
    for (unsigned i = 0; i < 1000; ++i)
    {
        assert(policy.request(false));
        assert(policy.request(true));
        policy.acknowledge(false);
        assert(policy.status().applied && policy.enabled());
    }
}
