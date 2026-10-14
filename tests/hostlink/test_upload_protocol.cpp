#include "track_sharing/upload_protocol.h"
#include <cassert>
#include <cstdio>
int main(int argc, char** argv)
{
    using namespace track_sharing;
    uint8_t body[471], id[16] = {1}, hash[32] = {2}, chunk[320] = {3};
    UploadRequest request;
    for (const auto kind : {UploadKind::AttachmentIndex, UploadKind::AttachmentResource, UploadKind::AttachmentBindings, UploadKind::SignedManifest})
    {
        for (const auto bytes : {uint32_t(0), uint32_t(1), uploadByteLimit(kind), uploadByteLimit(kind) + 1})
        {
            geocaching::protocol::CmpWriter writer(body, sizeof(body));
            assert(writer.array(4) && writer.binary({id, 16}) && writer.unsignedInteger(static_cast<uint8_t>(kind)) && writer.unsignedInteger(bytes) && writer.binary({hash, 32}));
            assert(decodeUploadRequest(UploadCommand::Begin, {body, writer.size()}, request) == (bytes > 0 && bytes <= uploadByteLimit(kind)));
        }
    }
    for (const auto bytes : {uint32_t(1), uint32_t(1100), uint32_t(1101)})
    {
        geocaching::protocol::CmpWriter writer(body, sizeof(body));
        assert(writer.array(4) && writer.binary({id, 16}) && writer.unsignedInteger(7) && writer.unsignedInteger(bytes) && writer.binary({hash, 32}));
        assert(decodeUploadRequest(UploadCommand::Begin, {body, writer.size()}, request) == (bytes <= 1100));
        if (bytes <= 1100) assert(request.kind == UploadKind::IpReceipt && request.total == bytes);
    }
    if (argc == 3)
    {
        const auto command = !std::strcmp(argv[1], "begin") ? UploadCommand::Begin : !std::strcmp(argv[1], "chunk") ? UploadCommand::Chunk
                                                                                 : !std::strcmp(argv[1], "finish")  ? UploadCommand::Finish
                                                                                                                    : static_cast<UploadCommand>(255);
        const size_t length = std::strlen(argv[2]);
        if (!length || length % 2 || length > sizeof(body) * 2) return 1;
        const auto digit = [](char value)
        { return value >= '0' && value <= '9' ? value - '0' : value >= 'a' && value <= 'f' ? value - 'a' + 10
                                                                                           : -1; };
        for (size_t i = 0; i < length / 2; ++i)
        {
            const int a = digit(argv[2][i * 2]), b = digit(argv[2][i * 2 + 1]);
            if (a < 0 || b < 0) return 1;
            body[i] = uint8_t(a * 16 + b);
        }
        if (!decodeUploadRequest(command, {body, length / 2}, request)) return 1;
        std::printf("%u,%u,%u\n", request.total, request.offset, static_cast<unsigned>(request.chunk.size));
        UploadJournal journal;
        std::memcpy(journal.id, request.id.data, 16);
        journal.author_hash[0] = 1;
        journal.sequence = 1;
        journal.generation = 1;
        journal.total = command == UploadCommand::Begin ? request.total : command == UploadCommand::Chunk ? request.offset + uint32_t(request.chunk.size)
                                                                                                          : 640;
        journal.durable_offset = command == UploadCommand::Begin ? 0 : journal.total;
        journal.phase = command == UploadCommand::Finish ? UploadPhase::Sealed : UploadPhase::Receiving;
        size_t size = 0;
        if (!encodeUploadProgress(journal, body, sizeof(body), size)) return 1;
        for (size_t i = 0; i < size; ++i) std::printf("%02x", body[i]);
        std::puts("");
        return 0;
    }
    geocaching::protocol::CmpWriter begin(body, sizeof(body));
    assert(begin.array(4) && begin.binary({id, 16}) && begin.unsignedInteger(3) && begin.unsignedInteger(8388608) && begin.binary({hash, 32}));
    assert(decodeUploadRequest(UploadCommand::Begin, {body, begin.size()}, request) && request.total == 8388608 && request.hash.size == 32);
    assert(!decodeUploadRequest(UploadCommand::Begin, {body, begin.size() - 1}, request));
    body[begin.size()] = 0;
    assert(!decodeUploadRequest(UploadCommand::Begin, {body, begin.size() + 1}, request));
    for (unsigned mode = 0; mode < 4; ++mode)
    {
        geocaching::protocol::CmpWriter writer(body, sizeof(body));
        assert(writer.array(4) && writer.binary({id, 16}) && writer.unsignedInteger(mode == 3 ? 5 : 4) && writer.unsignedInteger(mode == 1 ? 4097 : mode == 2 ? 0
                                                                                                                                                              : 4096) &&
               writer.binary({hash, 32}));
        assert(decodeUploadRequest(UploadCommand::Begin, {body, writer.size()}, request) == (mode == 0));
        if (mode == 0) assert(request.kind == UploadKind::Manifest && request.total == 4096);
    }
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        geocaching::protocol::CmpWriter writer(body, sizeof(body));
        assert(writer.array(3) && writer.binary({id, 16}) && writer.unsignedInteger(mode == 1 ? 8388608 : mode == 2 ? 8388607
                                                                                                                    : 0) &&
               writer.binary({chunk, 320}));
        assert(decodeUploadRequest(UploadCommand::Chunk, {body, writer.size()}, request) == (mode == 0));
    }
    geocaching::protocol::CmpWriter finish(body, sizeof(body));
    assert(finish.array(1) && finish.binary({id, 16}));
    assert(decodeUploadRequest(UploadCommand::Finish, {body, finish.size()}, request));
    body[3] = 0;
    assert(!decodeUploadRequest(UploadCommand::Finish, {body, finish.size()}, request));
    UploadJournal journal;
    journal.id[0] = 1;
    journal.author_hash[0] = 2;
    journal.generation = 1;
    journal.sequence = 1;
    journal.total = 640;
    journal.durable_offset = 640;
    size_t written = 0;
    for (const auto phase : {UploadPhase::Checking, UploadPhase::Sealed, UploadPhase::Failed})
    {
        journal.phase = phase;
        const bool ok = encodeUploadProgress(journal, body, sizeof(body), written);
        assert(ok == (phase != UploadPhase::Failed));
        if (!ok)
        {
            assert(!written);
            continue;
        }
        geocaching::protocol::CmpReader reader({body, written});
        size_t fields;
        geocaching::ByteView received;
        uint64_t value;
        assert(reader.array(fields, 4) && reader.binary(received, 16) && reader.unsignedInteger(value) && value == 320 && reader.unsignedInteger(value) && value == 640 && reader.unsignedInteger(value) && value == (phase == UploadPhase::Sealed ? 1 : 0) && reader.finished());
    }
}
