#include "track_sharing/network_upload_response.h"
#include "track_sharing/persist_upload_response.h"
#include "track_sharing/upload_reply_receiver.h"
#include "track_sharing/upload_send_step.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

struct Io
{
    enum class Mode
    {
        Read,
        CreateTemporary
    };
    enum class OpenStatus
    {
        Ready,
        Missing,
        Unavailable
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    std::string path;
    bool active = false, fail_create = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        if (mode == Mode::Read && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary && fail_create) return {nullptr, OpenStatus::Unavailable};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        return {this, OpenStatus::Ready};
    }
    bool size(void*, uint64_t& out)
    {
        out = files[path].size();
        return true;
    }
    size_t read(void*, uint8_t* out, size_t count)
    {
        count = std::min(count, files[path].size());
        std::memcpy(out, files[path].data(), count);
        return count;
    }
    size_t write(void*, const uint8_t* data, size_t count)
    {
        files[path].assign(data, data + count);
        return count;
    }
    bool sync(void*) { return true; }
    bool close(void*)
    {
        active = false;
        return true;
    }
};

struct FileIo : Io
{
    size_t offset = 0;
    bool seek(void*, uint64_t position)
    {
        offset = size_t(position);
        return offset <= files[path].size();
    }
    size_t read(void*, void* out, size_t count)
    {
        count = std::min(count, files[path].size() - offset);
        std::memcpy(out, files[path].data() + offset, count);
        offset += count;
        return count;
    }
};
struct Sender
{
    bool allowed = true;
    FileIo& io;
    unsigned calls = 0;
    std::vector<uint8_t> payload;
    chat::MeshSendResult sendTrackData(const uint8_t* directory, chat::lxmf::ByteSpan bytes, const uint8_t*, uint32_t generation, bool response, std::array<uint8_t, 32>*)
    {
        assert(!io.active && directory[0] == 1 && generation == 9 && !response);
        ++calls;
        payload.assign(bytes.data, bytes.data + bytes.size);
        return allowed ? chat::MeshSendResult::success(42) : chat::MeshSendResult::fail(chat::MeshOperationFailure::Busy);
    }
};

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    auto load = [&](const char* name)
    {
        std::ifstream input(std::string(argv[1]) + "/" + name, std::ios::binary);
        assert(input.good());
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
    };
    auto begin = load("begin.bin"), ack = load("ack.bin"), sealed = load("sealed.bin"), existing = load("existing.bin"), hash = load("hash.bin");
    std::array<uint8_t, 16> request{}, token{};
    request.fill('r');
    const geocaching::ByteView id{request.data(), 16};
    track_sharing::NetworkUploadBegin result;
    assert(track_sharing::decodeTrackUploadBegin({begin.data(), begin.size()}, id, 1024, result));
    assert(!result.sealed && result.offset == 0 && result.chunk_bytes == 1024 && result.token.size == 16);
    std::memcpy(token.data(), result.token.data, 16);
    assert(track_sharing::decodeTrackUploadAck({ack.data(), ack.size()}, id, {token.data(), 16}, 0, 1024, 1024));
    assert(!track_sharing::decodeTrackUploadAck({ack.data(), ack.size()}, id, {token.data(), 16}, 0, 1023, 1024));
    assert(track_sharing::decodeTrackUploadSealed({sealed.data(), sealed.size()}, id, {hash.data(), hash.size()}, 1024));
    assert(track_sharing::decodeTrackUploadBegin({existing.data(), existing.size()}, id, 1024, result));
    assert(result.sealed && result.offset == 1024 && result.token.size == 0);
    {
        using namespace track_sharing;
        Io io;
        PublicationJournalStore<Io> store(io, "a", "b");
        PublicationJournal current, next, verify;
        std::array<uint8_t, kPublicationJournalBytes> scratch{};
        for (const auto field : {geocaching::ByteView{current.operation, 16}, {current.object, 32}, {current.revision_hash, 32}, {current.directory, 16}, {current.directory_epoch, 16}, {current.author_hash, 32}})
            std::memset(const_cast<uint8_t*>(field.data), 1, field.size);
        std::memcpy(current.file_hash, hash.data(), 32);
        current.sequence = 1;
        current.generation = 9;
        current.revision = 1;
        current.file_bytes = 1024;
        assert(store.initialize(current, verify, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
        const auto initial_files = io.files;
        const auto initial_job = current;
        assert(persistTrackUploadBegin(store, current, next, verify, {begin.data(), begin.size()}, id, result, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
        assert(current.sequence == 2 && current.phase == PublicationPhase::Uploading && current.offset == 0);
        const auto started = io.files;
        io.fail_create = true;
        assert(persistTrackUploadAck(store, current, next, verify, {ack.data(), ack.size()}, id, {token.data(), 16}, 0, 1024, scratch.data(), scratch.size()) == PublicationStoreResult::StorageFailure);
        assert(current.sequence == 2 && current.offset == 0 && io.files == started);
        io.fail_create = false;
        assert(persistTrackUploadAck(store, current, next, verify, {ack.data(), ack.size()}, id, {token.data(), 16}, 0, 1024, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
        assert(current.sequence == 3 && current.offset == 1024);
        const auto uploaded = io.files;
        assert(persistTrackUploadAck(store, current, next, verify, {ack.data(), ack.size()}, id, {token.data(), 16}, 0, 1024, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
        assert(current.sequence == 3 && io.files == uploaded);
        assert(persistTrackUploadBegin(store, current, next, verify, {begin.data(), begin.size()}, id, result, scratch.data(), scratch.size()) == PublicationStoreResult::Conflict);
        assert(current.offset == 1024 && io.files == uploaded);
        assert(persistTrackUploadSealed(store, current, next, verify, {sealed.data(), sealed.size()}, id, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
        assert(current.sequence == 4 && current.phase == PublicationPhase::FileStored);
        const auto stored = io.files;
        assert(persistTrackUploadSealed(store, current, next, verify, {sealed.data(), sealed.size()}, id, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
        assert(current.sequence == 4 && io.files == stored);
        PublicationJournal recovered, other;
        assert(store.load(recovered, other, scratch.data(), scratch.size()) == PublicationStoreResult::Ready);
        assert(recovered.offset == 1024 && recovered.phase == PublicationPhase::FileStored && recovered.sequence == 4);
        io.files = initial_files;
        current = initial_job;
        struct Lease
        {
            bool valid = true;
            bool holds(const PublicationJournal&) const { return valid; }
        } lease;
        uint8_t local[16], message_hash[32]{};
        std::memset(local, 2, 16);
        UploadReplyReceiver<decltype(store), Lease> receiver(store, lease, current, next, verify, local, scratch.data(), scratch.size());
        static_assert(sizeof(receiver) <= 160, "Receiver stores bounded metadata only");
        assert(receiver.arm(4, id));
        assert(!receiver.arm(4, id));
        io.files = initial_files;
        current = initial_job;
        UploadReplyReceiver<decltype(store), Lease> sending(store, lease, current, next, verify, local, scratch.data(), scratch.size());
        FileIo file_io;
        file_io.files["kml"] = std::vector<uint8_t>(1024, '\n');
        const char prefix[] = "<kml/>";
        std::memcpy(file_io.files["kml"].data(), prefix, 6);
        Sender sender{true, file_io, 0, {}};
        uint8_t author[64]{}, wire[320]{}, file_buffer[256]{};
        const auto send_step = [&]
        { return sendTrackUploadStep(file_io, sending, sender, current, "kml", id, author, wire, sizeof(wire), file_buffer, sizeof(file_buffer)); };
        sender.allowed = false;
        assert(send_step() == UploadSendResult::Rejected && !sending.pending() && current.sequence == 1);
        sender.allowed = true;
        assert(send_step() == UploadSendResult::Queued && sending.pending());
        const auto calls = sender.calls;
        assert(send_step() == UploadSendResult::Waiting && sender.calls == calls);
        const auto receive_step = [&](const std::vector<uint8_t>& bytes)
        {
            chat::lxmf::CustomDeliveryView incoming{{current.directory, 16}, {local, 16}, {message_hash, 32}, {bytes.data(), bytes.size()}};
            return chat::lxmf::deliverVerifiedTrack(&sending.binding(), false, false, incoming);
        };
        assert(receive_step(begin) && current.phase == PublicationPhase::Uploading);
        file_io.files["kml"].pop_back();
        assert(send_step() == UploadSendResult::Conflict && !sending.pending() && !file_io.active && current.offset == 0);
        file_io.files["kml"].push_back('\n');
        for (uint32_t offset = 0; offset < 1024; offset += 256)
        {
            assert(send_step() == UploadSendResult::Queued && sending.pending() && current.offset == offset);
            geocaching::protocol::CmpReader packet({sender.payload.data(), sender.payload.size()});
            size_t fields = 0;
            uint64_t value = 0;
            geocaching::ByteView decoded;
            assert(packet.array(fields, 6) && fields == 6);
            assert(packet.unsignedInteger(value) && value == 1);
            assert(packet.unsignedInteger(value) && value == 0);
            assert(packet.unsignedInteger(value) && value == 5);
            assert(packet.binary(decoded, 16) && decoded.size == 16);
            assert(packet.unsignedInteger(value) && value == 8192);
            assert(packet.array(fields, 3) && fields == 3);
            assert(packet.binary(decoded, 16) && decoded.size == 16 && !std::memcmp(decoded.data, token.data(), 16));
            assert(packet.unsignedInteger(value) && value == offset);
            assert(packet.binary(decoded, 256) && decoded.size == 256 && !std::memcmp(decoded.data, file_io.files["kml"].data() + offset, 256) && packet.finished());
            const auto confirmation = load(("ack" + std::to_string(offset + 256) + ".bin").c_str());
            assert(receive_step(confirmation) && current.offset == offset + 256);
        }
        assert(send_step() == UploadSendResult::Queued && sending.pending());
        assert(receive_step(sealed) && current.phase == PublicationPhase::FileStored);
        assert(send_step() == UploadSendResult::FileStored && !sending.pending());
        io.files = initial_files;
        current = initial_job;
        chat::lxmf::CustomDeliveryView view{{current.directory, 16}, {local, 16}, {message_hash, 32}, {begin.data(), begin.size()}};
        const auto deliver = [&]
        { return chat::lxmf::deliverVerifiedTrack(&receiver.binding(), false, false, view); };
        lease.valid = false;
        assert(!deliver() && receiver.pending() && current.sequence == 1);
        lease.valid = true;
        uint8_t wrong[16]{};
        view.source = {wrong, 16};
        assert(!deliver() && current.sequence == 1);
        view.source = {current.directory, 16};
        view.destination = {wrong, 16};
        assert(!deliver() && current.sequence == 1);
        view.destination = {local, 16};
        assert(deliver() && !receiver.pending() && current.sequence == 2 && receiver.token().size == 16 && receiver.chunkLimit() == 1024);
        assert(!receiver.arm(6, id));
        assert(receiver.arm(5, id, 1024));
        view.data = {begin.data(), begin.size()};
        assert(!deliver() && receiver.pending());
        view.data = {ack.data(), ack.size()};
        io.fail_create = true;
        assert(!deliver() && receiver.pending() && current.offset == 0);
        io.fail_create = false;
        assert(deliver() && !receiver.pending() && current.offset == 1024 && current.sequence == 3);
        assert(!receiver.arm(5, id, 1));
        assert(receiver.arm(6, id));
        view.data = {sealed.data(), sealed.size()};
        assert(deliver() && !receiver.pending() && current.phase == PublicationPhase::FileStored && current.sequence == 4);
        assert(!receiver.arm(4, id));
    }
    for (size_t length = 0; length < begin.size(); ++length)
        assert(!track_sharing::decodeTrackUploadBegin({begin.data(), length}, id, 1024, result));
    for (size_t length = 0; length < ack.size(); ++length)
        assert(!track_sharing::decodeTrackUploadAck({ack.data(), length}, id, {token.data(), 16}, 0, 1024, 1024));
    for (size_t length = 0; length < sealed.size(); ++length)
        assert(!track_sharing::decodeTrackUploadSealed({sealed.data(), length}, id, {hash.data(), hash.size()}, 1024));
    request[0] ^= 1;
    assert(!track_sharing::decodeTrackUploadBegin({begin.data(), begin.size()}, id, 1024, result));
    assert(!track_sharing::decodeTrackUploadAck({ack.data(), ack.size()}, id, {token.data(), 16}, 0, 1024, 1024));
    assert(!track_sharing::decodeTrackUploadSealed({sealed.data(), sealed.size()}, id, {hash.data(), hash.size()}, 1024));
    request[0] ^= 1;
    token[0] ^= 1;
    assert(!track_sharing::decodeTrackUploadAck({ack.data(), ack.size()}, id, {token.data(), 16}, 0, 1024, 1024));
    hash[0] ^= 1;
    assert(!track_sharing::decodeTrackUploadSealed({sealed.data(), sealed.size()}, id, {hash.data(), hash.size()}, 1024));
    begin.push_back(0);
    assert(!track_sharing::decodeTrackUploadBegin({begin.data(), begin.size()}, id, 1024, result));
}
