#include "hostlink/snapshot_space_budget.h"
#include <cassert>
using hostlink::management::SnapshotSpaceBudget;
int main()
{
    SnapshotSpaceBudget full(8388608);
    assert(full.add(8388672, false) && full.complete());
    assert(!full.add(1, false));
    SnapshotSpaceBudget count(1);
    for (unsigned i = 0; i < 7; ++i) assert(count.add(0, false));
    assert(count.complete());
    assert(!count.add(0, false));
    SnapshotSpaceBudget directory(1);
    assert(!directory.add(0, true) && !directory.complete());
    SnapshotSpaceBudget invalid(0);
    assert(!invalid.complete());
    SnapshotSpaceBudget overflow(1);
    assert(!overflow.add(UINT64_MAX, false));
}
