#pragma once
#include "platform/esp/idf_common/hostlink_track_publication_runtime.h"
#include "track_sharing/read_usb_request.h"
namespace hostlink::management::track_read_runtime
{
struct ReadDecodeWork
{
    track_sharing::ReadUsbRequest decoded;
    uint8_t author[64]{}, request[16]{};
};
static_assert(sizeof(ReadDecodeWork) <= 512, "Read command decoder is transient PSRAM metadata");
inline Status processReadBegin(chat::IMeshAdapter& adapter, const EnvelopeView& input, uint64_t now,
                               uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!input.session || input.code != uint16_t(Operation::TrackReadBegin) || !output || capacity < 32) return Status::InvalidField;
    void* memory = heap_caps_malloc(sizeof(ReadDecodeWork), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return Status::Busy;
    auto* work = new (memory) ReadDecodeWork;
    Status status = Status::InvalidField;
    if (track_sharing::decodeReadUsbRequest({input.body, input.body_size}, work->decoded))
    {
        auto& decoded = work->decoded;
        std::memcpy(work->request, decoded.request.data, 16);
        uint32_t generation = 0;
        const auto author = adapter.readDeviceAuthorKey(work->author, generation);
        if (author != chat::DeviceAuthorResult::Ok)
            status = author == chat::DeviceAuthorResult::Unsupported ? Status::Unsupported : author == chat::DeviceAuthorResult::StorageError ? Status::StorageUnavailable
                                                                                                                                              : Status::Busy;
        else if (generation != decoded.generation) status = Status::StaleIdentity;
        else status = publication_runtime::beginRead(adapter, decoded.operation, decoded.request, {input.session, 16}, decoded.directory, decoded.epoch,
                                                     {work->author, 64}, generation, decoded.expected, now);
        if (status == Status::Ok)
        {
            // Owner has copied every frame view before the response overwrites
            // command.payload. ACK uses the independent decode workspace.
            geocaching::protocol::CmpWriter writer(output, capacity);
            if (!writer.array(2) || !writer.binary({work->request, 16}) || !writer.unsignedInteger(decoded.operation)) status = Status::Internal;
            else written = writer.size();
        }
    }
    work->~ReadDecodeWork();
    heap_caps_free(work);
    return status;
}
inline Status processReadResult(chat::IMeshAdapter& adapter, const EnvelopeView& input, uint64_t now,
                                uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!input.session || !output || capacity < 64) return Status::InvalidField;
    const bool chunk = input.code == uint16_t(Operation::TrackReadChunk);
    const bool release = input.code == uint16_t(Operation::TrackReadRelease);
    if (!chunk && !release && input.code != uint16_t(Operation::TrackReadStatus)) return Status::InvalidField;
    geocaching::protocol::CmpReader reader({input.body, input.body_size});
    geocaching::ByteView request;
    size_t fields = 0;
    uint64_t offset = 0, count = 0;
    if (!reader.array(fields, 3) || fields != (chunk ? 3u : 1u) || !reader.binary(request, 16) || !track_sharing::networkRequestId(request) ||
        (chunk && (!reader.unsignedInteger(offset) || offset > 8191 || !reader.unsignedInteger(count) || !count || count > 320)) || !reader.finished()) return Status::InvalidField;
    uint8_t id[16];
    std::memcpy(id, request.data, 16);
    namespace runtime = publication_runtime;
    if (release)
    {
        if (!runtime::controller || !runtime::controller->read) return Status::Expired;
        if (runtime::controller->adapter != &adapter || !runtime::controller->read->matchesSession(input.session)) return Status::Unauthorized;
        if (!runtime::controller->read->matchesRequest({id, 16})) return Status::Conflict;
        runtime::controller->read_retiring = true;
        if (!TrackReadOwner::release(runtime::controller->read)) return Status::Busy;
        runtime::controller->read_retiring = false;
        runtime::controller->read_state = track_sharing::ReadExchangeState::Idle;
        runtime::controller->last = now;
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    track_sharing::StoredReadResponse metadata;
    size_t ignored = 0;
    auto status = runtime::readResult(adapter, {input.session, 16}, {id, 16}, 0, 0, output, capacity, ignored, metadata, now);
    if (status != Status::Ok) return status;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!chunk)
    {
        if (!writer.array(5) || !writer.binary({id, 16}) || !writer.unsignedInteger(metadata.operation) || !writer.unsignedInteger(metadata.status) ||
            !writer.unsignedInteger(metadata.bytes) || !writer.binary({metadata.digest, 32})) return Status::Internal;
        written = writer.size();
        return Status::Ok;
    }
    if (offset >= metadata.bytes || count > metadata.bytes - offset) return Status::InvalidField;
    if (!writer.array(4) || !writer.binary({metadata.digest, 32}) || !writer.unsignedInteger(metadata.bytes) ||
        !writer.unsignedInteger(offset) || !writer.binaryHeader(count)) return Status::Internal;
    const size_t header = writer.size();
    if (count > capacity - header) return Status::LimitExceeded;
    size_t copied = 0;
    status = runtime::readResult(adapter, {input.session, 16}, {id, 16}, uint32_t(offset), uint16_t(count), output + header, capacity - header, copied, metadata, now);
    if (status != Status::Ok || copied != count) return status == Status::Ok ? Status::StorageUnavailable : status;
    written = header + copied;
    return Status::Ok;
}
} // namespace hostlink::management::track_read_runtime
