#include "track_sharing/manifest_chain.h"
#include <cassert>
int main()
{
    uint8_t author[64] = {1}, nonce[16] = {2}, hash[32] = {3}, file[32] = {4}, raw = 0;
    track_sharing::ManifestView parent, child;
    parent.raw = {&raw, 1};
    parent.author = {author, 64};
    parent.nonce = {nonce, 16};
    parent.revision = 1;
    parent.created = 100;
    parent.updated = 100;
    parent.file_hash = {file, 32};
    parent.file_bytes = 200;
    child = parent;
    child.revision = 2;
    child.previous = {hash, 32};
    child.updated = 101;
    assert(track_sharing::validateSuccessor(parent, child, hash));
    child.state = 1;
    assert(track_sharing::validateSuccessor(parent, child, hash));
    child.file_bytes++;
    assert(!track_sharing::validateSuccessor(parent, child, hash));
    child.file_bytes--;
    child.created++;
    assert(!track_sharing::validateSuccessor(parent, child, hash));
    child.created--;
    child.updated = 99;
    assert(!track_sharing::validateSuccessor(parent, child, hash));
    child.updated = 101;
    child.revision = 3;
    assert(!track_sharing::validateSuccessor(parent, child, hash));
    child.revision = 2;
    uint8_t different[32] = {9};
    assert(!track_sharing::validateSuccessor(parent, child, different));
    parent.state = 1;
    child.state = 0;
    assert(!track_sharing::validateSuccessor(parent, child, hash));
    parent.revision = UINT32_MAX;
    child.revision = 0;
    assert(!track_sharing::validateSuccessor(parent, child, hash));
}
