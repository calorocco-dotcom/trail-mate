#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include <cstring>
#include <initializer_list>
namespace track_sharing
{
struct CommitRequest
{
    geocaching::ByteView operation, object, revision_hash, directory, directory_epoch;
    uint32_t generation = 0, revision = 0;
};
static_assert(sizeof(CommitRequest) <= 96, "Commit request borrows bindings only");
// No manifest or private key. Decoding does not grant publication authority;
// runtime rechecks signed head, current author, retained blob and directory.
inline bool decodeCommitRequest(geocaching::ByteView body, CommitRequest& output)
{
    const auto source = reinterpret_cast<uintptr_t>(body.data), target = reinterpret_cast<uintptr_t>(&output);
    if (body.data && (source <= target ? target - source < body.size : source - target < sizeof(output))) return false;
    output = {};
    if (!body.data || !body.size || body.size > 133) return false;
    geocaching::protocol::CmpReader reader(body);
    size_t fields = 0;
    uint64_t generation = 0, revision = 0;
    if (!reader.array(fields, 7) || fields != 7 || !reader.binary(output.operation, 16) || output.operation.size != 16 ||
        !reader.binary(output.object, 32) || output.object.size != 32 || !reader.binary(output.revision_hash, 32) || output.revision_hash.size != 32 ||
        !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.unsignedInteger(revision) || !revision || revision > UINT32_MAX ||
        !reader.binary(output.directory, 16) || output.directory.size != 16 ||
        !reader.binary(output.directory_epoch, 16) || output.directory_epoch.size != 16 || !reader.finished()) return false;
    for (const auto field : {output.operation, output.object, output.revision_hash, output.directory, output.directory_epoch})
    {
        uint8_t nonzero = 0;
        for (size_t i = 0; i < field.size; ++i) nonzero |= field.data[i];
        if (!nonzero) return false;
    }
    output.generation = uint32_t(generation);
    output.revision = uint32_t(revision);
    return true;
}
} // namespace track_sharing
