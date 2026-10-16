#pragma once
#include "track_sharing/directory_announcement.h"
#include "track_sharing/directory_capabilities.h"

namespace track_sharing
{
struct TrackDirectoryEntry
{
    uint8_t discovery[16]{}, delivery[16]{}, public_key[64]{}, epoch[16]{};
    char name[41]{};
    uint64_t last_seen = 0;
    uint32_t file_limit = 0;
    uint16_t chunk_limit = 0;
    uint8_t query_limit = 0;
    bool capabilities_verified = false;
};
static_assert(sizeof(TrackDirectoryEntry) <= 176, "Directory entry contains metadata only");

// Entries are caller-admitted PSRAM storage. Router callbacks and snapshot
// readers must be serialized; no backing pointer escapes its ownership lease.
class TrackDirectoryPool
{
  public:
    static constexpr size_t kCapacity = 8;
    TrackDirectoryPool(TrackDirectoryEntry* entries, size_t capacity)
        : entries_(entries), capacity_(entries ? (capacity < kCapacity ? capacity : kCapacity) : 0) {}
    size_t size() const { return count_; }
    const TrackDirectoryEntry* at(size_t index) const { return index < count_ ? &entries_[index] : nullptr; }
    bool observe(const chat::lxmf::TrackDirectoryAnnouncementView& view, uint64_t now)
    {
        DirectoryAdvertisement advertisement;
        if (!decodeTrackDirectoryAnnouncement(view, advertisement)) return false;
        TrackDirectoryEntry* entry = nullptr;
        for (size_t i = 0; i < count_; ++i)
            if (!std::memcmp(entries_[i].discovery, view.discovery_destination.data, 16))
            {
                entry = &entries_[i];
                break;
            }
        if (entry)
        {
            if (now < entry->last_seen || std::memcmp(entry->delivery, advertisement.delivery.data, 16) ||
                std::memcmp(entry->public_key, advertisement.public_key.data, 64)) return false;
        }
        else
        {
            if (count_ == capacity_) return false;
            // One delivery identity cannot appear as another discovery service.
            for (size_t i = 0; i < count_; ++i)
                if (!std::memcmp(entries_[i].delivery, advertisement.delivery.data, 16)) return false;
            entry = &entries_[count_++];
            *entry = {};
            std::memcpy(entry->discovery, view.discovery_destination.data, 16);
            std::memcpy(entry->delivery, advertisement.delivery.data, 16);
            std::memcpy(entry->public_key, advertisement.public_key.data, 64);
        }
        if (std::memcmp(entry->epoch, advertisement.epoch.data, 16))
        {
            std::memcpy(entry->epoch, advertisement.epoch.data, 16);
            entry->capabilities_verified = false;
            entry->file_limit = 0;
            entry->chunk_limit = 0;
            entry->query_limit = 0;
        }
        std::memset(entry->name, 0, sizeof(entry->name));
        std::memcpy(entry->name, advertisement.name.data(), advertisement.name.size());
        entry->last_seen = now;
        return true;
    }
    const TrackDirectoryEntry* find(geocaching::ByteView delivery, geocaching::ByteView epoch) const
    {
        if (!networkRequestId(delivery) || !networkRequestId(epoch)) return nullptr;
        for (size_t i = 0; i < count_; ++i)
            if (!std::memcmp(entries_[i].delivery, delivery.data, 16) && !std::memcmp(entries_[i].epoch, epoch.data, 16)) return &entries_[i];
        return nullptr;
    }
    bool confirmCapabilities(geocaching::ByteView delivery, geocaching::ByteView epoch,
                             geocaching::ByteView response, geocaching::ByteView request)
    {
        TrackDirectoryCapabilities capabilities;
        const auto* selected = find(delivery, epoch);
        if (!selected || !decodeTrackCapabilities(response, request, capabilities) || capabilities.operations != 0x1ff ||
            std::memcmp(capabilities.public_key.data, selected->public_key, 64) || std::memcmp(capabilities.epoch.data, selected->epoch, 16) ||
            !networkInputDisjoint(response, reinterpret_cast<uint8_t*>(const_cast<TrackDirectoryEntry*>(selected)), sizeof(*selected))) return false;
        auto* entry = &entries_[selected - entries_];
        entry->file_limit = capabilities.file_limit;
        entry->chunk_limit = capabilities.chunk_limit;
        entry->query_limit = capabilities.query_limit;
        entry->capabilities_verified = true;
        return true;
    }

  private:
    TrackDirectoryEntry* entries_;
    size_t capacity_;
    size_t count_ = 0;
};
static_assert(sizeof(TrackDirectoryPool) <= 24, "Pool borrows all entry storage");
} // namespace track_sharing
