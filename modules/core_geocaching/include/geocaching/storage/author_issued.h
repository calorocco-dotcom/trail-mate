#pragma once
#include "geocaching/storage/stored_time.h"

namespace geocaching::storage
{
struct AuthorIssuedView
{
    ByteView cache_id;
    uint32_t revision = 0;
    ByteView revision_hash;
    ByteView author_public_key;
    StoredTime issued_at;
    // Present only for receipt-verified historical recovery. Ordinary issuance
    // retains the three-field representation and first-revision time check.
    uint64_t recovered_created_at = 0;
    bool has_recovered_creation = false;
};
inline bool decodeAuthorIssued(ByteView key, ByteView value, AuthorIssuedView& out)
{
    out = {};
    if (!key.data || key.size != 36 || !value.data || value.size > 32768) return false;
    AuthorIssuedView candidate;
    candidate.cache_id = {key.data, 32};
    for (unsigned i = 0; i < 4; ++i) candidate.revision = (candidate.revision << 8) | key.data[32 + i];
    if (!candidate.revision) return false;
    protocol::CmpReader reader(value);
    size_t fields = 0;
    if (!reader.array(fields, 4) || (fields != 3 && fields != 4) || !reader.binary(candidate.revision_hash, 32) || candidate.revision_hash.size != 32 ||
        !reader.binary(candidate.author_public_key, 64) || candidate.author_public_key.size != 64 ||
        !decodeStoredTime(reader, candidate.issued_at)) return false;
    candidate.has_recovered_creation=fields==4;
    if(candidate.has_recovered_creation&&(!reader.unsignedInteger(candidate.recovered_created_at)||
       !candidate.issued_at.has_utc||candidate.recovered_created_at>candidate.issued_at.utc_seconds))return false;
    if(!reader.finished())return false;
    out = candidate;
    return true;
}
inline bool encodeAuthorIssued(const RevisionHash& hash, ByteView public_key, const StoredTime& issued_at,
                               uint8_t* output, size_t capacity, size_t& written, uint64_t recovered_created_at = UINT64_MAX)
{
    written = 0;
    const bool recovered=recovered_created_at!=UINT64_MAX;
    if (!public_key.data || public_key.size != 64 || (recovered&&(!issued_at.has_utc||recovered_created_at>issued_at.utc_seconds))) return false;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(recovered?4:3) || !writer.binary({hash.bytes.data(), 32}) || !writer.binary(public_key) ||
        !encodeStoredTime(writer, issued_at) || (recovered&&!writer.unsignedInteger(recovered_created_at))) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage
