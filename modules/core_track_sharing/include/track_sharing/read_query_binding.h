#pragma once
#include "track_sharing/network_read.h"

namespace track_sharing
{
// Hash lends its admitted stream context. Output and CMP1 workspace are caller
// storage, independent of query/cursor inputs. No implicit hash context/buffer.
template <class Hash>
bool deriveReadQueryBinding(const ReadQuery& query, Hash& hash, uint8_t* workspace,
                            size_t capacity, uint8_t output[32])
{
    if (!workspace || !capacity || !output || query.west < -1800000000 || query.west > 1799999999 ||
        query.east < -1800000000 || query.east > 1799999999 || query.south < -900000000 ||
        query.north > 900000000 || query.south > query.north || query.kind < -1 || query.kind > 1 ||
        query.state < -1 || query.state > 1 || (query.author.size && !readSpan(query.author, 64))) return false;
    for (const auto input : {geocaching::ByteView{reinterpret_cast<const uint8_t*>(&query), sizeof(query)},
                             query.author, query.cursor_object, query.cursor_binding})
    {
        if (input.size && (!networkInputDisjoint(input, workspace, capacity) || !networkInputDisjoint(input, output, 32))) return false;
    }
    if (!networkInputDisjoint({output, 32}, workspace, capacity)) return false;
    geocaching::protocol::CmpWriter writer(workspace, capacity);
    if (!writer.array(4) || !writer.array(4) || !writer.signedInteger(query.west) || !writer.signedInteger(query.south) ||
        !writer.signedInteger(query.east) || !writer.signedInteger(query.north) ||
        !(query.kind < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.kind))) ||
        !(query.author.size ? writer.binary(query.author) : writer.nil()) ||
        !(query.state < 0 ? writer.nil() : writer.unsignedInteger(uint8_t(query.state)))) return false;
    const bool ok = hash.start() && hash.update(workspace, writer.size()) && hash.finish(output);
    hash.reset();
    if (!ok) std::memset(output, 0, 32);
    return ok;
}
} // namespace track_sharing
