#pragma once
#include "track_sharing/network_upload_response.h"

namespace track_sharing
{
struct ReadResponseView
{
    geocaching::ByteView body;
    uint16_t status = 0;
    uint8_t fields = 0;
};
inline bool readResponseOperation(uint8_t operation) { return operation == 0 || operation == 1 || operation == 2 || operation == 8 || (operation >= 9 && operation <= 13); }

// Envelope validation is separate from operation-specific validation. A sink
// must validate the complete body before accepting or acknowledging storage.
inline bool decodeTrackReadEnvelope(geocaching::ByteView bytes, geocaching::ByteView request,
                                    uint8_t operation, ReadResponseView& output)
{
    if (!bytes.data || !bytes.size || bytes.size > 8192 || !networkRequestId(request) || !readResponseOperation(operation) ||
        !networkInputDisjoint(bytes, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader(bytes);
    size_t count = 0;
    uint64_t value = 0, status = 0;
    geocaching::ByteView id;
    if (!reader.array(count, 6) || count != 6 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 1 || !reader.unsignedInteger(value) || value != operation ||
        !reader.binary(id, 16) || id.size != 16 || std::memcmp(id.data, request.data, 16) ||
        !reader.unsignedInteger(status) || (status != 200 && (status < 400 || status > 599))) return false;
    const size_t body_start = reader.position();
    if (!reader.array(count, 8)) return false;
    if (status == 200)
    {
        const size_t expected = operation == 0 || operation == 9 ? 8 : operation == 1 || operation == 10 || operation == 11 || operation == 13 ? 3
                                                 : operation == 2   ? 2
                                                                    : 4;
        if (count != expected) return false;
    }
    else
    {
        std::string_view diagnostic;
        if ((count != 1 && count != 2) || !reader.text(diagnostic, 128) ||
            (count == 2 && (!reader.unsignedInteger(value) || value > UINT32_MAX)) || !reader.finished()) return false;
    }
    output = {{bytes.data + body_start, bytes.size - body_start}, uint16_t(status), uint8_t(count)};
    return true;
}

struct ReadManifestResponse
{
    geocaching::ByteView manifest, signature;
    bool current = false;
};
inline bool decodeTrackReadManifestResponse(geocaching::ByteView bytes, geocaching::ByteView request, ReadManifestResponse& output)
{
    if (!bytes.data || !bytes.size || bytes.size > 8192 ||
        !networkInputDisjoint(bytes, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader(bytes);
    ReadManifestResponse candidate;
    size_t count = 0;
    uint64_t current = 0;
    if (!uploadResponseHeader(reader, 2, request, 2) || !reader.array(count, 2) || count != 2 ||
        !reader.binary(candidate.manifest, 4096) || !candidate.manifest.size ||
        !reader.binary(candidate.signature, 64) || candidate.signature.size != 64 ||
        !reader.unsignedInteger(current) || current > 1 || !reader.finished()) return false;
    candidate.current = current != 0;
    output = candidate;
    return true;
}

struct ReadFileResponse
{
    geocaching::ByteView content;
    uint32_t total = 0, offset = 0;
};
inline bool decodeTrackReadFileResponse(geocaching::ByteView bytes, geocaching::ByteView request,
                                        geocaching::ByteView expected_hash, uint32_t expected_total,
                                        uint32_t expected_offset, uint16_t expected_length, ReadFileResponse& output, uint8_t operation = 8)
{
    if ((operation != 8 && operation != 12) || !bytes.data || !bytes.size || bytes.size > 8192 || !expected_hash.data || expected_hash.size != 32 ||
        !expected_total || expected_total > 8388608 || expected_offset >= expected_total || !expected_length ||
        expected_length > 1024 || expected_length > expected_total - expected_offset ||
        !networkInputDisjoint(bytes, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(expected_hash, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader(bytes);
    geocaching::ByteView hash, content;
    uint64_t total = 0, offset = 0;
    if (!uploadResponseHeader(reader, operation, request, 4) || !reader.binary(hash, 32) || hash.size != 32 ||
        std::memcmp(hash.data, expected_hash.data, 32) || !reader.unsignedInteger(total) || total != expected_total ||
        !reader.unsignedInteger(offset) || offset != expected_offset || !reader.binary(content, 1024) ||
        content.size != expected_length || !reader.finished()) return false;
    output = {content, uint32_t(total), uint32_t(offset)};
    return true;
}
} // namespace track_sharing
