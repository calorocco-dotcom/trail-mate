#pragma once
#include "track_sharing/network_publish.h"

namespace track_sharing
{
enum class PublicationSendResult : uint8_t
{
    Queued,
    Waiting,
    Rejected,
    StorageFailure,
    Conflict,
    Invalid
};
// Borrow caller-admitted job/proof/records and separate wire/journal buffers.
// No file handle remains open while entering the protected router. The durable
// operation ID is the network request ID on every retry, including after reboot.
template <class Store, class Receiver, class Sender>
PublicationSendResult sendTrackPublicationStep(Store& store, Receiver& receiver, Sender& sender,
                                               PublicationJournal& current, PublicationJournal& next, PublicationJournal& verify,
                                               const VerifiedManifestView& manifest, const uint8_t author[64],
                                               uint8_t* output, size_t capacity, uint8_t* scratch, size_t scratch_capacity)
{
    if (!publicationManifestMatches(current, manifest) ||
        (current.phase != PublicationPhase::FileStored && current.phase != PublicationPhase::Publishing) ||
        !author || !manifest.manifest.author.data || manifest.manifest.author.size != 64 ||
        std::memcmp(author, manifest.manifest.author.data, 64) || !output || !scratch || scratch_capacity < kPublicationJournalBytes ||
        &current == &next || &current == &verify || &next == &verify || signingSpansOverlap(output, capacity, scratch, scratch_capacity)) return PublicationSendResult::Invalid;
    const geocaching::ByteView inputs[] = {
        {reinterpret_cast<const uint8_t*>(&current), sizeof(current)}, {reinterpret_cast<const uint8_t*>(&next), sizeof(next)}, {reinterpret_cast<const uint8_t*>(&verify), sizeof(verify)}, {reinterpret_cast<const uint8_t*>(&manifest), sizeof(manifest)}, {reinterpret_cast<const uint8_t*>(&receiver), sizeof(receiver)}, {reinterpret_cast<const uint8_t*>(&sender), sizeof(sender)}, {reinterpret_cast<const uint8_t*>(&store), sizeof(store)}, {author, 64}, manifest.manifest.raw, manifest.signature, manifest.manifest.previous};
    for (const auto input : inputs)
        if (input.size && (!networkInputDisjoint(input, output, capacity) || !networkInputDisjoint(input, scratch, scratch_capacity))) return PublicationSendResult::Invalid;
    if (receiver.pending()) return PublicationSendResult::Waiting;
    // Preflight encoding before changing any durable state. The shadow's phase
    // affects admission only; wire bindings exclude the journal sequence.
    next = current;
    next.phase = PublicationPhase::Publishing;
    size_t written = 0;
    if (!encodeTrackPublishRequest(next, manifest, output, capacity, written)) return PublicationSendResult::Invalid;
    const auto durable = markPublicationSending(store, current, next, verify, manifest, scratch, scratch_capacity);
    if (durable != PublicationStoreResult::Ready) return durable == PublicationStoreResult::StorageFailure ? PublicationSendResult::StorageFailure : PublicationSendResult::Conflict;
    if (!receiver.arm()) return PublicationSendResult::Invalid;
    const auto sent = sender.sendTrackData(current.directory, {output, written}, author, current.generation, false, nullptr);
    if (!sent.ok)
    {
        receiver.cancelPending();
        return PublicationSendResult::Rejected;
    }
    return PublicationSendResult::Queued;
}
} // namespace track_sharing
