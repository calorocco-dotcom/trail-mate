#include "track_sharing/read_reply_receiver.h"
#include "track_sharing/read_response_sink.h"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

struct Store
{
    bool available = true;
    unsigned calls = 0;
    std::vector<uint8_t> saved;
    track_sharing::ReadStoreResult persist(geocaching::ByteView raw, uint8_t*, size_t)
    {
        calls++;
        if (!available) return track_sharing::ReadStoreResult::StorageFailure;
        saved.assign(raw.data, raw.data + raw.size);
        return track_sharing::ReadStoreResult::Ready;
    }
};
struct Validator
{
    bool signature_valid = true;
    std::array<uint8_t, 32> file_hash{};
    bool validate(uint8_t operation, geocaching::ByteView request, const track_sharing::ReadResponseView& response, geocaching::ByteView raw)
    {
        if (!signature_valid) return false;
        if (response.status != 200) return true;
        track_sharing::ReadFileResponse file;
        return operation == 8 && track_sharing::decodeTrackReadFileResponse(raw, request, {file_hash.data(), file_hash.size()}, 3, 0, 3, file);
    }
};
// Exercises the hash-port lifecycle, not a substitute for SHA-256 validation.
struct Hash
{
    bool available = true, active = false;
    unsigned resets = 0;
    std::vector<uint8_t> input;
    bool start()
    {
        input.clear();
        active = available;
        return active;
    }
    bool update(const uint8_t* bytes, size_t size)
    {
        if (!active) return false;
        input.assign(bytes, bytes + size);
        return true;
    }
    bool finish(uint8_t* output)
    {
        if (!active) return false;
        std::memset(output, 9, 32);
        active = false;
        return available;
    }
    void reset()
    {
        active = false;
        resets++;
    }
};
struct Lease
{
    bool holds() const { return true; }
};
int main()
{
    std::array<uint8_t, 16> request{}, directory{}, local{};
    std::array<uint8_t, 64> author{};
    std::array<uint8_t, 32> message{};
    std::array<uint8_t, 512> raw{};
    std::array<uint8_t, 128> scratch{};
    const uint8_t content[] = {1, 2, 3};
    request.fill(1);
    directory.fill(2);
    local.fill(3);
    author.fill(4);
    message.fill(5);
    Store store;
    Validator validator;
    validator.file_hash.fill(6);
    Hash hash;
    Lease lease;
    track_sharing::StoredReadResponse result;
    track_sharing::ReadResponseSink<Store, Validator, Hash> sink(store, validator, hash, result, scratch.data(), scratch.size());
    static_assert(sizeof(sink) <= 64, "Sink cannot retain response buffers");
    track_sharing::ReadReplyReceiver<decltype(sink), Lease> receiver(sink, lease, {author.data(), author.size()}, 7);
    assert(receiver.arm(8, {request.data(), request.size()}, {directory.data(), directory.size()}, {local.data(), local.size()}, 512));
    geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
    assert(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(8) && writer.binary({request.data(), request.size()}) && writer.unsignedInteger(200) && writer.array(4));
    assert(writer.binary({validator.file_hash.data(), validator.file_hash.size()}) && writer.unsignedInteger(3) && writer.unsignedInteger(0) && writer.binary({content, 3}));
    chat::lxmf::CustomDeliveryView view{{directory.data(), 16}, {local.data(), 16}, {message.data(), 32}, {raw.data(), writer.size()}};
    auto deliver = [&]
    { return chat::lxmf::deliverVerifiedTrack(&receiver.binding(), false, false, view); };
    validator.signature_valid = false;
    assert(!deliver() && !result.ready && store.calls == 0);
    validator.signature_valid = true;
    store.available = false;
    assert(!deliver() && receiver.pending() && !result.ready);
    store.available = true;
    hash.available = false;
    assert(!deliver() && receiver.pending() && !result.ready && hash.resets == 1);
    hash.available = true;
    assert(deliver() && result.ready && !receiver.pending());
    assert(result.bytes == writer.size() && result.status == 200 && result.operation == 8);
    assert(hash.input == store.saved && hash.input.size() == writer.size());
    const unsigned calls = store.calls;
    assert(deliver() && store.calls == calls);
}
