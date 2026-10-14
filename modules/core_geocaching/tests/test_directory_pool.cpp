#include "geocaching/usecase/directory_pool.h"
#include <cassert>
#include <memory>

int main()
{
    std::array<geocaching::DirectoryEntry, geocaching::DirectoryPool::kCapacity> entries{};
    auto pool = std::make_unique<geocaching::DirectoryPool>(entries.data(), entries.size());
    std::array<std::uint8_t, 64> key{};
    geocaching::Destination discovery;
    geocaching::protocol::DirectoryAnnouncement announce;
    announce.name = "Public";
    announce.sequence = 5;
    assert(pool->next(0, false) == nullptr);
    for (unsigned i = 0; i < 32; ++i)
    {
        discovery.bytes[0] = static_cast<std::uint8_t>(i);
        announce.delivery.bytes[0] = static_cast<std::uint8_t>(i);
        assert(pool->observe(discovery, announce, {key.data(), key.size()}, 1));
    }
    assert(pool->size() == 32);
    for (unsigned i = 0; i < 32; ++i)
    {
        auto* entry = pool->next(1, false);
        assert(entry && entry->discovery.bytes[0] == i);
        entry->capabilities_verified = true;
    }
    assert(pool->next(1, false) == nullptr);
    auto* ready = pool->next(1, true);
    assert(ready && ready->sequence == 5);
    ready->in_flight = true;
    assert(pool->next(1, true) != ready);
    discovery = ready->discovery;
    announce.delivery = ready->delivery;
    ready->max_query_items = 20;
    ready->retry_after = 100;
    announce.sequence = 6;
    assert(pool->observe(discovery, announce, {key.data(), key.size()}, 2));
    assert(ready->capabilities_verified && ready->max_query_items == 20);
    assert(ready->in_flight && ready->retry_after == 100);
    announce.epoch[0] = 1;
    announce.sequence = 1;
    assert(pool->observe(discovery, announce, {key.data(), key.size()}, 3));
    assert(!ready->capabilities_verified && ready->max_query_items == 0);
    assert(ready->sequence == 1 && ready->retry_after == 0);
    assert(ready->in_flight);
    discovery.bytes[0] = 32;
    announce.delivery.bytes[0] = 32;
    assert(!pool->observe(discovery, announce, {key.data(), key.size()}, 2));
    assert(pool->size() == 32);
}
