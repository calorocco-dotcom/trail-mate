#pragma once
#include "chat/ports/i_geocaching_transport.h"

namespace chat::lxmf
{
struct TrackDirectoryAnnouncementView
{
    ByteSpan discovery_destination, delivery_destination, public_key, app_data;
};
using TrackDirectoryAnnouncementHandler = bool (*)(const TrackDirectoryAnnouncementView&, void*);
// Installed binding must outlive its registration. The router serializes
// installation, invocation and removal; callback views remain borrowed.
struct TrackDeliveryBinding
{
    CustomDeliveryHandler handler = nullptr;
    void* context = nullptr;
    TrackDirectoryAnnouncementHandler announcement = nullptr;
    // Only ephemeral control exchanges (e.g. capabilities): true requires
    // complete synchronous processing, never merely volatile queueing.
    bool (*control)(const CustomDeliveryView&, void*) = nullptr;
    // Optional immutable local-account lease, borrowed until unregistration.
    // Backend checks it against its own identity before entering callbacks;
    // callbacks must not reenter the router to fetch current author material.
    const uint8_t* expected_local_author = nullptr;
    uint32_t expected_author_generation = 0;
};

// Normal Reticulum ingestion has verified signature and service destination.
// Consumer validates the application schema and copies admitted metadata only.
inline bool deliverVerifiedTrackDirectory(const TrackDeliveryBinding* binding, const TrackDirectoryAnnouncementView& view)
{
    return binding && binding->announcement && view.discovery_destination.data && view.discovery_destination.size == 16 &&
           view.delivery_destination.data && view.delivery_destination.size == 16 && view.public_key.data && view.public_key.size == 64 &&
           view.app_data.data && view.app_data.size && view.app_data.size <= 128 && binding->announcement(view, binding->context);
}

inline bool isTrackCustomType(ByteSpan type)
{
    constexpr char name[] = "trailmate.track.v1";
    return type.data && type.size == sizeof(name) - 1 &&
           std::memcmp(type.data, name, sizeof(name) - 1) == 0;
}

inline bool deliverVerifiedTrack(const TrackDeliveryBinding* binding,
                                 bool source_unverified, bool group,
                                 const CustomDeliveryView& view)
{
    if (!(!source_unverified && !group && binding &&
          view.source.data && view.source.size == 16 &&
          view.destination.data && view.destination.size == 16 &&
          view.message_hash.data && view.message_hash.size == 32 &&
          view.data.data && view.data.size && view.data.size <= 8192)) return false;
    if (binding->control && binding->control(view, binding->context)) return true;
    // Business acceptance still requires durable processing before returning
    // true. Returning false retains transport retry semantics.
    return binding->handler && binding->handler(view, binding->context);
}

template <class Identity>
bool trackBindingAuthorMatches(const TrackDeliveryBinding* binding, const Identity& identity)
{
    if (!binding) return false;
    if (!binding->expected_local_author && !binding->expected_author_generation) return true;
    const auto* expected = binding->expected_local_author;
    return expected && binding->expected_author_generation && identity.isReady() && !identity.identityStorageError() &&
           identity.authorGeneration() == binding->expected_author_generation && identity.encryptionPublicKey() && identity.signingPublicKey() &&
           !std::memcmp(expected,identity.encryptionPublicKey(),32) && !std::memcmp(expected + 32,identity.signingPublicKey(),32);
}
template <class Identity>
bool deliverVerifiedTrackForAuthor(const TrackDeliveryBinding* binding, bool source_unverified, bool group,
                                    const CustomDeliveryView& view, const Identity& identity)
{
    return trackBindingAuthorMatches(binding, identity) && deliverVerifiedTrack(binding, source_unverified, group, view);
}
} // namespace chat::lxmf
