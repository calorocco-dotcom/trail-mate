#include "track_sharing/object_head.h"
#include <cassert>
int main()
{
    using namespace track_sharing;
    ObjectHead first, decoded;
    first.object[0] = 1;
    first.hash[0] = 2;
    first.operation[0] = 3;
    first.generation = 7;
    first.revision = 1;
    first.sequence = 1;
    uint8_t raw[kObjectHeadBytes];
    assert(encodeObjectHead(first, raw, sizeof(raw)) && decodeObjectHead(raw, sizeof(raw), decoded) && decoded.revision == 1);
    for (size_t i = 0; i < sizeof(raw); ++i)
    {
        raw[i] ^= 1;
        assert(!decodeObjectHead(raw, sizeof(raw), decoded) && !decoded.sequence);
        raw[i] ^= 1;
    }
    auto committed = first;
    committed.sequence = 2;
    committed.phase = HeadPhase::Committed;
    assert(objectHeadAdvanceValid(first, committed));
    auto rebound = committed;
    ++rebound.sequence;
    rebound.generation = 8;
    assert(objectHeadAdvanceValid(committed, rebound));
    rebound.phase = HeadPhase::Signed;
    assert(!objectHeadAdvanceValid(committed, rebound));
    rebound.phase = HeadPhase::Committed;
    rebound.hash[0] ^= 1;
    assert(!objectHeadAdvanceValid(committed, rebound));
    rebound.hash[0] ^= 1;
    rebound.operation[0] ^= 1;
    assert(!objectHeadAdvanceValid(committed, rebound));
    rebound.operation[0] ^= 1;
    rebound.archived = true;
    assert(!objectHeadAdvanceValid(committed, rebound));
    auto stale_signed = first;
    ++stale_signed.sequence;
    stale_signed.generation = 8;
    assert(!objectHeadAdvanceValid(first, stale_signed));
    auto next = committed;
    next.sequence = 3;
    next.revision = 2;
    next.phase = HeadPhase::Signed;
    std::memcpy(next.parent, first.hash, 32);
    next.hash[0] = 4;
    next.operation[0] = 5;
    assert(objectHeadAdvanceValid(committed, next) && !objectHeadAdvanceValid(first, next));
    next.parent[0] ^= 1;
    assert(!objectHeadAdvanceValid(committed, next));
    next.parent[0] ^= 1;
    next.revision = 3;
    assert(!objectHeadAdvanceValid(committed, next));
    next.revision = 2;
    next.generation = 8;
    assert(!objectHeadAdvanceValid(committed, next));
    next.generation = 7;
    next.archived = true;
    assert(objectHeadAdvanceValid(committed, next));
    auto archived = next;
    archived.sequence = 4;
    archived.phase = HeadPhase::Committed;
    assert(objectHeadAdvanceValid(next, archived));
    auto reopened = archived;
    reopened.sequence = 5;
    reopened.revision = 3;
    reopened.phase = HeadPhase::Signed;
    reopened.archived = false;
    std::memcpy(reopened.parent, archived.hash, 32);
    reopened.hash[0] = 6;
    reopened.operation[0] = 7;
    assert(!objectHeadAdvanceValid(archived, reopened));
    first.sequence = UINT64_MAX;
    assert(encodeObjectHead(first, raw, sizeof(raw)) && decodeObjectHead(raw, sizeof(raw), decoded) && decoded.sequence == UINT64_MAX);
    assert(!objectHeadAdvanceValid(first, committed));
}
