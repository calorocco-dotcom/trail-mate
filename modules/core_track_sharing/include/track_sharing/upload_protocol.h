#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "track_sharing/upload_journal.h"
#include "track_sharing/upload_paths.h"
namespace track_sharing
{
enum class UploadCommand : uint8_t
{
    Begin,
    Chunk,
    Finish,
    Status
};
struct UploadRequest
{
    geocaching::ByteView id, hash, chunk;
    uint32_t total = 0, offset = 0;
    UploadKind kind = UploadKind::Kml;
};
// Views borrow the command body. Platform must retain/copy scalar bindings and
// move chunk bytes out of the scratch prefix before journal I/O overwrites it.
inline bool decodeUploadRequest(UploadCommand command, geocaching::ByteView body, UploadRequest& output)
{
    output = {};
    if (!body.data || !body.size || body.size > 471 || static_cast<uint8_t>(command) > 3) return false;
    geocaching::protocol::CmpReader reader(body);
    size_t fields = 0;
    const size_t count = command == UploadCommand::Begin ? 4 : command == UploadCommand::Chunk ? 3
                                                                                               : 1;
    if (!reader.array(fields, count) || fields != count || !reader.binary(output.id, 16) || output.id.size != 16) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < 16; ++i) bits |= output.id.data[i];
    if (!bits) return false;
    uint64_t value = 0;
    if (command == UploadCommand::Begin)
    {
        if (!reader.unsignedInteger(value) || value > UINT8_MAX || !uploadByteLimit(static_cast<UploadKind>(value))) return false;
        output.kind = static_cast<UploadKind>(value);
        if (!reader.unsignedInteger(value) || !value || value > uploadByteLimit(output.kind)) return false;
        output.total = uint32_t(value);
        if (!reader.binary(output.hash, 32) || output.hash.size != 32) return false;
    }
    else if (command == UploadCommand::Chunk)
    {
        if (!reader.unsignedInteger(value) || value >= 8388608 || !reader.binary(output.chunk, 320) || !output.chunk.size || output.chunk.size > 8388608 - value) return false;
        output.offset = uint32_t(value);
    }
    return reader.finished();
}
// Failed is returned as an error status by dispatcher, never as sealed=0.
inline bool encodeUploadProgress(const UploadJournal& journal, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!uploadJournalValid(journal) || journal.phase == UploadPhase::Failed || !output) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !writer.binary({journal.id, 16}) || !writer.unsignedInteger(320) || !writer.unsignedInteger(journal.durable_offset) || !writer.unsignedInteger(journal.phase == UploadPhase::Sealed ? 1 : 0)) return false;
    written = writer.size();
    return true;
}
} // namespace track_sharing
