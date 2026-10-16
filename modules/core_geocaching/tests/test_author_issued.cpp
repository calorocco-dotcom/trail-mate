#include "geocaching/storage/author_issued.h"
int main()
{
    using namespace geocaching;
    uint8_t key[36]{}, public_key[64]{}, bytes[192]{};
    key[35] = 1;
    public_key[0] = 7;
    RevisionHash hash;
    hash.bytes[0] = 9;
    storage::StoredTime time;
    time.monotonic_ms = 123;
    size_t size = 0;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size)) return 1;
    storage::AuthorIssuedView out;
    if (!storage::decodeAuthorIssued({key, 36}, {bytes, size}, out) || out.revision != 1 ||
        out.author_public_key.data[0] != 7 || out.revision_hash.data[0] != 9 || out.issued_at.monotonic_ms != 123 || out.has_recovered_creation || bytes[0] != 0x93) return 2;
    key[35] = 0;
    if (storage::decodeAuthorIssued({key, 36}, {bytes, size}, out)) return 3;
    time.utc_trusted = true;
    if (storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size) || size) return 4;
    key[35] = 3;
    time.utc_trusted = false;
    time.has_utc = true;
    time.utc_seconds = 0;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size, 0) || bytes[0] != 0x94 ||
        !storage::decodeAuthorIssued({key, 36}, {bytes, size}, out) || !out.has_recovered_creation || out.recovered_created_at != 0) return 5;
    time.utc_seconds = 100;
    if (!storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size, 50) ||
        !storage::decodeAuthorIssued({key, 36}, {bytes, size}, out) || out.recovered_created_at != 50) return 6;
    if (storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size, 101) || size) return 7;
    time.has_utc = false;
    if (storage::encodeAuthorIssued(hash, {public_key, 64}, time, bytes, sizeof(bytes), size, 0) || size) return 8;
    return 0;
}
