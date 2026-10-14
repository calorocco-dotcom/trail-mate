#include "track_sharing/directory_pool.h"
#include <cassert>

int main()
{
    using namespace track_sharing;
    TrackDirectoryEntry entries[1]{};
    TrackDirectoryPool pool(entries, 1);
    uint8_t discovery[16], delivery[16], public_key[64], epoch[16], metadata[128];
    std::memset(discovery, 1, 16);
    std::memset(delivery, 2, 16);
    std::memset(public_key, 3, 64);
    std::memset(epoch, 4, 16);
    auto encode = [&]
    {
        geocaching::protocol::CmpWriter writer(metadata, sizeof(metadata));
        assert(writer.array(5) && writer.unsignedInteger(1) && writer.text(kApplicationType) && writer.binary({delivery, 16}) && writer.binary({epoch, 16}) && writer.text("Directory"));
        return writer.size();
    };
    chat::lxmf::TrackDirectoryAnnouncementView view{{discovery, 16}, {delivery, 16}, {public_key, 64}, {metadata, encode()}};
    assert(pool.observe(view, 100) && pool.size() == 1 && pool.at(0) && !pool.at(1));
    assert(pool.find({delivery, 16}, {epoch, 16}) == pool.at(0));
    uint8_t request[16], response[256];
    std::memset(request, 5, 16);
    auto capability_response = [&](uint16_t chunk = 1024, uint8_t operations = 9)
    {
        geocaching::protocol::CmpWriter writer(response, sizeof(response));
        assert(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(0) && writer.binary({request, 16}) &&
               writer.unsignedInteger(200) && writer.array(8) && writer.unsignedInteger(1) && writer.text(kApplicationType) && writer.binary({public_key, 64}) &&
               writer.binary({epoch, 16}) && writer.unsignedInteger(8388608) && writer.unsignedInteger(chunk) && writer.unsignedInteger(32) && writer.array(operations));
        for (unsigned i = 0; i < operations; ++i) assert(writer.unsignedInteger(i));
        return writer.size();
    };
    auto response_size = capability_response();
    assert(pool.confirmCapabilities({delivery, 16}, {epoch, 16}, {response, response_size}, {request, 16}));
    assert(entries[0].capabilities_verified && entries[0].file_limit == 8388608 && entries[0].chunk_limit == 1024 && entries[0].query_limit == 32);
    for (size_t length = 0; length < response_size; ++length)
        assert(!pool.confirmCapabilities({delivery, 16}, {epoch, 16}, {response, length}, {request, 16}));
    request[0] ^= 1;
    assert(!pool.confirmCapabilities({delivery, 16}, {epoch, 16}, {response, response_size}, {request, 16}));
    request[0] ^= 1;
    response_size = capability_response(0);
    assert(!pool.confirmCapabilities({delivery, 16}, {epoch, 16}, {response, response_size}, {request, 16}));
    response_size = capability_response(1024, 8);
    assert(!pool.confirmCapabilities({delivery, 16}, {epoch, 16}, {response, response_size}, {request, 16}));
    public_key[0] ^= 1;
    response_size = capability_response();
    public_key[0] ^= 1;
    assert(!pool.confirmCapabilities({delivery, 16}, {epoch, 16}, {response, response_size}, {request, 16}));
    assert(pool.observe(view, 101) && entries[0].capabilities_verified);
    public_key[0] ^= 1;
    assert(!pool.observe(view, 102) && entries[0].public_key[0] == 3);
    public_key[0] ^= 1;
    assert(!pool.observe(view, 99) && entries[0].last_seen == 101);
    uint8_t old_epoch[16];
    std::memcpy(old_epoch, epoch, 16);
    epoch[0] ^= 1;
    view.app_data.size = encode();
    assert(pool.observe(view, 103) && !entries[0].capabilities_verified && !entries[0].file_limit && !entries[0].chunk_limit && !entries[0].query_limit);
    assert(!pool.find({delivery, 16}, {old_epoch, 16}) && pool.find({delivery, 16}, {epoch, 16}));
    response_size = capability_response();
    assert(pool.confirmCapabilities({delivery, 16}, {epoch, 16}, {response, response_size}, {request, 16}));
    assert(!pool.confirmCapabilities({delivery, 16}, {old_epoch, 16}, {response, response_size}, {request, 16}));
    discovery[0] ^= 1;
    delivery[0] ^= 1;
    view.app_data.size = encode();
    assert(!pool.observe(view, 104) && pool.size() == 1 && entries[0].last_seen == 103);
    TrackDirectoryPool empty(nullptr, 8);
    assert(!empty.observe(view, 104) && empty.size() == 0);
}
