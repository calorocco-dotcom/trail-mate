#include "track_sharing/read_network_exchange.h"
#include "track_sharing/network_read.h"
#include <array>
#include <cassert>

struct Lease { bool live = true; bool holds() const { return live; } };
struct Sink
{
    unsigned writes = 0;
    bool validateAndStore(uint8_t, geocaching::ByteView, const track_sharing::ReadResponseView& response, geocaching::ByteView)
    { if (response.status != 404) return false; ++writes; return true; }
};
struct Adapter
{
    const chat::lxmf::TrackDeliveryBinding* binding = nullptr;
    std::array<uint8_t, 64> author{};
    uint32_t generation = 9;
    unsigned sends = 0;
    bool locked = false, busy = false, send_ok = true;
    chat::TrackBindingChange replaceTrackDelivery(const chat::lxmf::TrackDeliveryBinding* old, const chat::lxmf::TrackDeliveryBinding* next)
    {
        if (busy) return chat::TrackBindingChange::Busy;
        if (old != binding) return chat::TrackBindingChange::Conflict;
        binding = next;
        return chat::TrackBindingChange::Applied;
    }
    chat::TrackBindingChange visitTrackDelivery(const chat::lxmf::TrackDeliveryBinding* expected, uint8_t* key, uint32_t& epoch, chat::TrackBindingVisit visit, void* context)
    {
        if (busy) return chat::TrackBindingChange::Busy;
        if (expected != binding) return chat::TrackBindingChange::Conflict;
        std::memcpy(key, author.data(), 64);
        epoch = generation;
        locked = true;
        visit(context);
        locked = false;
        return chat::TrackBindingChange::Applied;
    }
    struct Result { bool ok; };
    Result sendTrackData(const uint8_t*, chat::lxmf::ByteSpan bytes, const uint8_t* key, uint32_t epoch, bool response, void*)
    {
        assert(!locked && bytes.size && !response && epoch == generation && !std::memcmp(key, author.data(), 64));
        ++sends;
        return {send_ok};
    }
};
int main()
{
    using namespace track_sharing;
    using Change = chat::TrackBindingChange;
    std::array<uint8_t, 16> id{}, directory{}, local{};
    std::array<uint8_t, 32> object{}, message{};
    std::array<uint8_t, 320> wire{}, reply{};
    id.fill(1); directory.fill(2); local.fill(3); object.fill(4); message.fill(5);
    size_t bytes = 0;
    assert(encodeTrackReadManifest({id.data(), 16}, {object.data(), 32}, {}, 8192, wire.data(), wire.size(), bytes));
    geocaching::protocol::CmpWriter writer(reply.data(), reply.size());
    assert(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(2) &&
           writer.binary({id.data(), 16}) && writer.unsignedInteger(404) && writer.array(1) && writer.text({"missing", 7}));
    chat::lxmf::CustomDeliveryView view{{directory.data(),16},{local.data(),16},{message.data(),32},{reply.data(),writer.size()}};
    for (unsigned scenario = 0; scenario < 6; ++scenario)
    {
        Adapter adapter; adapter.author.fill(6);
        Sink sink; Lease lease;
        ReadNetworkExchange<Adapter,Sink,Lease> exchange(adapter,sink,lease,{adapter.author.data(),64},9);
        static_assert(sizeof(exchange) <= 768, "Exchange has only one small PSRAM request, no response queue");
        auto attach = [&] { return exchange.attach(2,{id.data(),16},{directory.data(),16},{local.data(),16},{wire.data(),bytes},8192,100); };
        adapter.busy = true;
        assert(attach() == Change::Busy && !exchange.attached());
        adapter.busy = false;
        assert(attach() == Change::Applied && exchange.attached());
        ReadExchangeState state = ReadExchangeState::Idle;
        if (scenario == 1) adapter.generation++;
        if (scenario == 2) lease.live = false;
        if (scenario == 3) adapter.send_ok = false;
        assert(exchange.advance(scenario == 4 ? 45100 : 101,state) == Change::Applied);
        if (scenario == 1 || scenario == 2) assert(state == ReadExchangeState::Stale && adapter.sends == 0);
        else if (scenario == 3) assert(state == ReadExchangeState::Rejected && adapter.sends == 1);
        else if (scenario == 4) assert(state == ReadExchangeState::Expired && adapter.sends == 0);
        else
        {
            assert(state == ReadExchangeState::Waiting && adapter.sends == 1);
            assert(exchange.advance(102,state) == Change::Applied && adapter.sends == 1);
            if (scenario == 0)
            {
                assert(chat::lxmf::deliverVerifiedTrack(adapter.binding,false,false,view));
                assert(exchange.advance(103,state) == Change::Applied && state == ReadExchangeState::Ready && sink.writes == 1);
                assert(chat::lxmf::deliverVerifiedTrack(adapter.binding,false,false,view) && sink.writes == 1);
            }
        }
        adapter.busy = true;
        assert(exchange.detach() == Change::Busy && exchange.attached());
        adapter.busy = false;
        assert(exchange.detach() == Change::Applied && !exchange.attached() && !adapter.binding);
        assert(exchange.advance(104,state) == Change::Conflict);
        if (scenario == 5) assert(state == ReadExchangeState::Cancelled);
    }
}
