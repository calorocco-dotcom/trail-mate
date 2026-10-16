#pragma once
#include "chat/ports/track_delivery_binding.h"
#include "geocaching/protocol/record_decoder.h"
#include "track_sharing/network_request.h"

namespace track_sharing
{
struct DirectoryAdvertisement
{
    geocaching::ByteView public_key, delivery, epoch;
    std::string_view name;
};
// Only call from verified Reticulum announcement ingestion. It supplies the
// public-key/service/LXMF binding; this decoder validates application metadata.
// All fields remain borrowed until the consumer copies admitted metadata.
inline bool decodeTrackDirectoryAnnouncement(const chat::lxmf::TrackDirectoryAnnouncementView& view, DirectoryAdvertisement& output)
{
    if (!view.discovery_destination.data || view.discovery_destination.size != 16 ||
        !view.delivery_destination.data || view.delivery_destination.size != 16 ||
        !view.public_key.data || view.public_key.size != 64 || !view.app_data.data || !view.app_data.size || view.app_data.size > 128) return false;
    for (const auto field : {view.discovery_destination, view.delivery_destination, view.public_key, view.app_data})
        if (!networkInputDisjoint({field.data, field.size}, reinterpret_cast<uint8_t*>(&output), sizeof(output))) return false;
    geocaching::protocol::CmpReader reader({view.app_data.data, view.app_data.size});
    size_t fields = 0;
    uint64_t version = 0;
    std::string_view type;
    DirectoryAdvertisement candidate;
    if (!reader.array(fields, 5) || fields != 5 || !reader.unsignedInteger(version) || version != 1 ||
        !reader.text(type, sizeof(kApplicationType) - 1) || type != kApplicationType || !reader.binary(candidate.delivery, 16) || !networkRequestId(candidate.delivery) ||
        std::memcmp(candidate.delivery.data, view.delivery_destination.data, 16) || !reader.binary(candidate.epoch, 16) || !networkRequestId(candidate.epoch) ||
        !reader.text(candidate.name, 40) || !geocaching::protocol::validRecordText(candidate.name, false, true) || !reader.finished()) return false;
    candidate.public_key = {view.public_key.data, view.public_key.size};
    output = candidate;
    return true;
}
} // namespace track_sharing
