#include "track_sharing/read_reply_receiver.h"
#include <array>
#include <cassert>
#include <cstring>

struct Lease
{
    bool live = true;
    bool holds() const { return live; }
};
struct Sink
{
    std::array<uint8_t, 32> hash{};
    std::array<uint8_t, 3> saved{};
    bool available = true;
    unsigned writes = 0;
    bool validateAndStore(uint8_t operation, geocaching::ByteView request,
                          const track_sharing::ReadResponseView& envelope, geocaching::ByteView raw)
    {
        if (!available) return false;
        if (envelope.status != 200)
        {
            ++writes;
            return true;
        }
        track_sharing::ReadFileResponse file;
        if (operation != 8 || !track_sharing::decodeTrackReadFileResponse(raw, request, {hash.data(), hash.size()}, 3, 0, 3, file)) return false;
        std::memcpy(saved.data(), file.content.data, 3);
        ++writes;
        return true;
    }
};
int main()
{
    std::array<uint8_t, 16> request{}, directory{}, local{};
    std::array<uint8_t, 32> message{};
    std::array<uint8_t, 64> author{}, signature{};
    std::array<uint8_t, 512> raw{};
    request.fill(1);
    directory.fill(2);
    local.fill(3);
    message.fill(4);
    author.fill(5);
    signature.fill(6);
    Sink sink;
    sink.hash.fill(7);
    Lease lease;
    track_sharing::ReadReplyReceiver<Sink, Lease> receiver(sink, lease, {author.data(), author.size()}, 9);
    static_assert(sizeof(receiver) <= 256, "Receiver must not retain payload buffers");
    const geocaching::ByteView id{request.data(), request.size()}, source{directory.data(), directory.size()}, destination{local.data(), local.size()};
    assert(receiver.arm(8, id, source, destination, 512));
    assert(!receiver.arm(8, id, source, destination, 512));
    auto file = [&]
    {
        geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
        assert(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(8) && writer.binary(id) && writer.unsignedInteger(200) && writer.array(4));
        assert(writer.binary({sink.hash.data(), sink.hash.size()}) && writer.unsignedInteger(3) && writer.unsignedInteger(0) && writer.binary({signature.data(), 3}));
        return writer.size();
    };
    size_t length = file();
    chat::lxmf::CustomDeliveryView view{{directory.data(), 16}, {local.data(), 16}, {message.data(), 32}, {raw.data(), length}};
    auto deliver = [&]
    { return chat::lxmf::deliverVerifiedTrack(&receiver.binding(), false, false, view); };
    assert(!chat::lxmf::deliverVerifiedTrack(&receiver.binding(), true, false, view));
    assert(!chat::lxmf::deliverVerifiedTrack(&receiver.binding(), false, true, view));
    local[0] ^= 1;
    assert(!deliver());
    local[0] ^= 1;
    raw[length] = 0;
    view.data.size++;
    assert(!deliver() && receiver.pending());
    view.data.size = length;
    sink.available = false;
    assert(!deliver() && receiver.pending());
    sink.available = true;
    lease.live = false;
    assert(!deliver() && receiver.pending());
    lease.live = true;
    assert(deliver() && !receiver.pending() && sink.writes == 1 && sink.saved[0] == 6);
    assert(deliver() && sink.writes == 1);
    message[0] ^= 1;
    assert(!deliver());
    message[0] ^= 1;
    receiver.cancel();
    assert(!deliver());
    assert(!receiver.arm(7, id, source, destination, 512));
    assert(receiver.arm(2, id, source, destination, 512));
    geocaching::protocol::CmpWriter writer(raw.data(), raw.size());
    assert(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(2) && writer.binary(id) && writer.unsignedInteger(200) && writer.array(2) && writer.array(2) && writer.binary({signature.data(), 3}) && writer.binary({signature.data(), 64}) && writer.unsignedInteger(1));
    track_sharing::ReadManifestResponse manifest;
    assert(track_sharing::decodeTrackReadManifestResponse({raw.data(), writer.size()}, id, manifest) && manifest.current && manifest.signature.size == 64);
    raw[writer.size()] = 0;
    assert(!track_sharing::decodeTrackReadManifestResponse({raw.data(), writer.size() + 1}, id, manifest));
    receiver.cancel();
}
