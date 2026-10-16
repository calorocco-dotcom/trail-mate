#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "track_sharing/network_request.h"

namespace track_sharing
{
struct NetworkUploadBegin
{
    geocaching::ByteView token;
    uint32_t offset = 0;
    uint16_t chunk_bytes = 0;
    bool sealed = false;
};
// Source/destination authentication is required before these decoders. The
// request ID and operation are matched to the outstanding durable job.
inline bool uploadResponseHeader(geocaching::protocol::CmpReader& reader,
                                 uint8_t operation, geocaching::ByteView request, size_t fields)
{
    size_t count = 0;
    uint64_t value = 0;
    geocaching::ByteView received;
    return networkRequestId(request) && reader.array(count, 6) && count == 6 &&
           reader.unsignedInteger(value) && value == 1 && reader.unsignedInteger(value) && value == 1 &&
           reader.unsignedInteger(value) && value == operation && reader.binary(received, 16) &&
           received.size == 16 && !std::memcmp(received.data, request.data, 16) &&
           reader.unsignedInteger(value) && value == 200 && reader.array(count, fields) && count == fields;
}
inline bool decodeTrackUploadBegin(geocaching::ByteView bytes, geocaching::ByteView request,
                                   uint32_t file_bytes, NetworkUploadBegin& output)
{
    if (!bytes.data || !bytes.size || bytes.size > 8192 || !file_bytes || file_bytes > 8388608 ||
        !networkInputDisjoint(bytes, reinterpret_cast<uint8_t*>(&output), sizeof(output)) ||
        !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader(bytes);
    if (!uploadResponseHeader(reader, 4, request, 4)) return false;
    NetworkUploadBegin candidate;
    if (reader.position() < bytes.size && bytes.data[reader.position()] == 0xc0)
    {
        if (!reader.nil()) return false;
    }
    else if (!reader.binary(candidate.token, 16) || !networkRequestId(candidate.token)) return false;
    uint64_t offset = 0, sealed = 0, chunk = 0;
    if (!reader.unsignedInteger(offset) || offset > file_bytes || !reader.unsignedInteger(sealed) || sealed > 1 ||
        !reader.unsignedInteger(chunk) || !chunk || chunk > kNetworkChunkBytes || !reader.finished() ||
        (sealed ? candidate.token.size != 0 || offset != file_bytes : candidate.token.size != 16)) return false;
    candidate.offset = uint32_t(offset);
    candidate.chunk_bytes = uint16_t(chunk);
    candidate.sealed = sealed != 0;
    output = candidate;
    return true;
}
inline bool decodeTrackUploadAck(geocaching::ByteView bytes, geocaching::ByteView request,
                                 geocaching::ByteView token, uint32_t offset, size_t chunk_bytes,
                                 uint32_t file_bytes)
{
    if (!bytes.data || !bytes.size || bytes.size > 8192 || !networkRequestId(token) ||
        !file_bytes || file_bytes > 8388608 || !chunk_bytes || chunk_bytes > kNetworkChunkBytes ||
        offset > file_bytes || chunk_bytes > file_bytes - offset) return false;
    geocaching::protocol::CmpReader reader(bytes);
    geocaching::ByteView received;
    uint64_t next = 0;
    return uploadResponseHeader(reader, 5, request, 2) && reader.binary(received, 16) && received.size == 16 &&
           !std::memcmp(received.data, token.data, 16) && reader.unsignedInteger(next) &&
           next == offset + chunk_bytes && reader.finished();
}
inline bool decodeTrackUploadSealed(geocaching::ByteView bytes, geocaching::ByteView request,
                                    geocaching::ByteView file_hash, uint32_t file_bytes)
{
    if (!bytes.data || !bytes.size || bytes.size > 8192 || !file_hash.data || file_hash.size != 32 ||
        !file_bytes || file_bytes > 8388608) return false;
    geocaching::protocol::CmpReader reader(bytes);
    geocaching::ByteView received;
    uint64_t length = 0, sealed = 0, cleanup = 0;
    return uploadResponseHeader(reader, 6, request, 4) && reader.binary(received, 32) && received.size == 32 &&
           !std::memcmp(received.data, file_hash.data, 32) && reader.unsignedInteger(length) && length == file_bytes &&
           reader.unsignedInteger(sealed) && sealed == 1 && reader.unsignedInteger(cleanup) && cleanup <= 1 && reader.finished();
}
} // namespace track_sharing
