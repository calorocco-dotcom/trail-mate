#pragma once
#include "track_sharing/upload_reply_receiver.h"
#include <algorithm>

namespace track_sharing
{
enum class UploadSendResult : uint8_t
{
    Queued,
    Waiting,
    FileStored,
    Rejected,
    StorageFailure,
    Conflict,
    Invalid
};

// Caller holds immutable retained-KML and author/media/directory leases. Output
// (320 bytes) and file scratch (256 bytes) are borrowed job workspace. No file
// handle remains open while entering the router. Sending never advances offset.
template <class Io, class Receiver, class Sender>
UploadSendResult sendTrackUploadStep(Io& io, Receiver& receiver, Sender& sender,
                                     const PublicationJournal& current, const char* retained_kml,
                                     geocaching::ByteView request, const uint8_t author[64],
                                     uint8_t* output, size_t capacity, uint8_t* file_scratch, size_t file_capacity)
{
    if (receiver.pending()) return UploadSendResult::Waiting;
    if (!publicationJournalValid(current)) return UploadSendResult::Invalid;
    if (current.phase == PublicationPhase::FileStored) return UploadSendResult::FileStored;
    if (!retained_kml || !*retained_kml || !author || !output || capacity < 320 || !file_scratch || !file_capacity ||
        !networkRequestId(request) || signingSpansOverlap(output, capacity, file_scratch, file_capacity)) return UploadSendResult::Invalid;
    const geocaching::ByteView inputs[] = {
        request, {author, 64}, {reinterpret_cast<const uint8_t*>(&current), sizeof(current)}, {reinterpret_cast<const uint8_t*>(&receiver), sizeof(receiver)}, {reinterpret_cast<const uint8_t*>(&io), sizeof(io)}, {reinterpret_cast<const uint8_t*>(&sender), sizeof(sender)}, {reinterpret_cast<const uint8_t*>(retained_kml), std::strlen(retained_kml) + 1}};
    for (const auto input : inputs)
        if (!networkInputDisjoint(input, output, capacity) || !networkInputDisjoint(input, file_scratch, file_capacity)) return UploadSendResult::Invalid;
    uint8_t operation = 4;
    size_t written = 0, count = 0;
    bool encoded = false;
    if (current.phase == PublicationPhase::Accepted ||
        (current.phase == PublicationPhase::Uploading && receiver.token().size != 16))
        encoded = encodeTrackUploadBegin(request, {current.file_hash, 32}, current.file_bytes, 8192, output, capacity, written);
    else if (current.phase == PublicationPhase::Uploading && current.offset < current.file_bytes)
    {
        operation = 5;
        count = std::min({size_t(256), file_capacity, size_t(receiver.chunkLimit()), size_t(current.file_bytes - current.offset)});
        if (!count) return UploadSendResult::Invalid;
        const auto input = io.open(retained_kml, Io::Mode::Read);
        if (!input.handle) return UploadSendResult::StorageFailure;
        uint64_t bytes = 0;
        const bool sized = io.size(input.handle, bytes);
        const bool read = sized && bytes == current.file_bytes && io.seek(input.handle, current.offset) &&
                          io.read(input.handle, file_scratch, count) == count;
        const bool closed = io.close(input.handle);
        if (!sized || !closed) return UploadSendResult::StorageFailure;
        if (bytes != current.file_bytes) return UploadSendResult::Conflict;
        if (!read) return UploadSendResult::StorageFailure;
        encoded = encodeTrackUploadChunk(request, receiver.token(), current.offset, {file_scratch, count}, 8192, output, capacity, written);
    }
    else if (current.phase == PublicationPhase::Uploading && current.offset == current.file_bytes)
    {
        operation = 6;
        encoded = encodeTrackUploadFinish(request, receiver.token(), 8192, output, capacity, written);
    }
    if (!encoded || !receiver.arm(operation, request, count)) return UploadSendResult::Invalid;
    const auto result = sender.sendTrackData(current.directory, {output, written}, author, current.generation, false, nullptr);
    if (!result.ok)
    {
        receiver.cancelPending();
        return UploadSendResult::Rejected;
    }
    return UploadSendResult::Queued;
}
} // namespace track_sharing
