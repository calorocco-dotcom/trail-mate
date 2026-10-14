#pragma once
#include "track_sharing/network_upload_response.h"

namespace track_sharing
{
struct TrackDirectoryCapabilities
{
    geocaching::ByteView public_key, epoch;
    uint32_t file_limit = 0;
    uint16_t chunk_limit = 0, operations = 0;
    uint8_t query_limit = 0;
};
// Transport must authenticate source/destination. Pool separately pins public
// key and incarnation to the verified announcement before accepting limits.
inline bool decodeTrackCapabilities(geocaching::ByteView bytes, geocaching::ByteView request, TrackDirectoryCapabilities& output)
{
    if (!bytes.data || !bytes.size || bytes.size > 8192 ||
        !networkInputDisjoint(bytes, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader(bytes);
    if (!uploadResponseHeader(reader, 0, request, 8)) return false;
    TrackDirectoryCapabilities candidate;
    uint64_t version = 0, file = 0, chunk = 0, query = 0, operation = 0;
    std::string_view type;
    size_t count = 0;
    if (!reader.unsignedInteger(version) || version != 1 || !reader.text(type, sizeof(kApplicationType) - 1) || type != kApplicationType ||
        !reader.binary(candidate.public_key, 64) || candidate.public_key.size != 64 || !reader.binary(candidate.epoch, 16) || !networkRequestId(candidate.epoch) ||
        !reader.unsignedInteger(file) || !file || file > 8388608 || !reader.unsignedInteger(chunk) || !chunk || chunk > 1024 ||
        !reader.unsignedInteger(query) || !query || query > 32 || !reader.array(count, 9) || !count) return false;
    for (size_t i = 0; i < count; ++i)
    {
        if (!reader.unsignedInteger(operation) || operation > 8 || (candidate.operations & (1U << operation))) return false;
        candidate.operations |= uint16_t(1U << operation);
    }
    if (!reader.finished()) return false;
    candidate.file_limit = uint32_t(file);
    candidate.chunk_limit = uint16_t(chunk);
    candidate.query_limit = uint8_t(query);
    output = candidate;
    return true;
}
} // namespace track_sharing
