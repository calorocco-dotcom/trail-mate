#pragma once
#include "track_sharing/network_upload_response.h"
#include "track_sharing/publication_journal_store.h"

namespace track_sharing
{
// Runtime holds verified directory/author/media leases. Three journal records
// and scratch belong to the admitted PSRAM job, never automatic ESP locals.
inline bool uploadJournalWorkspace(const PublicationJournal& current, PublicationJournal& next,
                                   PublicationJournal& verify, geocaching::ByteView response,
                                   geocaching::ByteView request, geocaching::ByteView token,
                                   uint8_t* scratch, size_t capacity)
{
    if (&current == &next || &current == &verify || &next == &verify || !scratch ||
        capacity < kPublicationJournalBytes || !publicationJournalValid(current)) return false;
    const geocaching::ByteView records[] = {
        {reinterpret_cast<const uint8_t*>(&current), sizeof(current)},
        {reinterpret_cast<const uint8_t*>(&next), sizeof(next)},
        {reinterpret_cast<const uint8_t*>(&verify), sizeof(verify)}};
    for (const auto record : records)
    {
        if (signingSpansOverlap(record.data, record.size, scratch, capacity) ||
            signingSpansOverlap(record.data, record.size, response.data, response.size) ||
            signingSpansOverlap(record.data, record.size, request.data, request.size) ||
            (token.size && signingSpansOverlap(record.data, record.size, token.data, token.size))) return false;
    }
    return !signingSpansOverlap(scratch, capacity, response.data, response.size) &&
           !signingSpansOverlap(scratch, capacity, request.data, request.size) &&
           (!token.size || !signingSpansOverlap(scratch, capacity, token.data, token.size));
}

template <class Store>
PublicationStoreResult persistTrackUploadBegin(Store& store, PublicationJournal& current,
                                               PublicationJournal& next, PublicationJournal& verify,
                                               geocaching::ByteView response, geocaching::ByteView request,
                                               NetworkUploadBegin& accepted, uint8_t* scratch, size_t capacity)
{
    if (!uploadJournalWorkspace(current, next, verify, response, request, {}, scratch, capacity) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&accepted), sizeof(accepted), scratch, capacity) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&accepted), sizeof(accepted), reinterpret_cast<const uint8_t*>(&current), sizeof(current)) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&accepted), sizeof(accepted), reinterpret_cast<const uint8_t*>(&next), sizeof(next)) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&accepted), sizeof(accepted), reinterpret_cast<const uint8_t*>(&verify), sizeof(verify)) ||
        !networkInputDisjoint(response, reinterpret_cast<uint8_t*>(&accepted), sizeof(accepted)) ||
        !networkInputDisjoint(request, reinterpret_cast<uint8_t*>(&accepted), sizeof(accepted)) ||
        (current.phase != PublicationPhase::Accepted && current.phase != PublicationPhase::Uploading)) return PublicationStoreResult::Invalid;
    NetworkUploadBegin candidate;
    if (!decodeTrackUploadBegin(response, request, current.file_bytes, candidate)) return PublicationStoreResult::Invalid;
    if (candidate.offset < current.offset) return PublicationStoreResult::Conflict;
    next = current;
    next.offset = candidate.offset;
    next.phase = candidate.sealed ? PublicationPhase::FileStored : PublicationPhase::Uploading;
    auto result = PublicationStoreResult::Invalid;
    if (next.offset == current.offset && next.phase == current.phase)
        result = store.validateCurrent(current, verify, scratch, capacity);
    else if (current.sequence != UINT64_MAX)
    {
        ++next.sequence;
        result = store.advance(current, next, verify, scratch, capacity);
    }
    if (result == PublicationStoreResult::Ready)
    {
        current = next;
        accepted = candidate;
    }
    return result;
}

template <class Store>
PublicationStoreResult persistTrackUploadAck(Store& store, PublicationJournal& current,
                                             PublicationJournal& next, PublicationJournal& verify,
                                             geocaching::ByteView response, geocaching::ByteView request,
                                             geocaching::ByteView token, uint32_t sent_offset, size_t sent_bytes,
                                             uint8_t* scratch, size_t capacity)
{
    if (!uploadJournalWorkspace(current, next, verify, response, request, token, scratch, capacity) ||
        current.phase != PublicationPhase::Uploading ||
        !decodeTrackUploadAck(response, request, token, sent_offset, sent_bytes, current.file_bytes)) return PublicationStoreResult::Invalid;
    const auto end = uint32_t(sent_offset + sent_bytes);
    if (current.offset == end) return store.validateCurrent(current, verify, scratch, capacity);
    if (current.offset != sent_offset) return PublicationStoreResult::Conflict;
    if (current.sequence == UINT64_MAX) return PublicationStoreResult::Invalid;
    next = current;
    next.offset = end;
    ++next.sequence;
    const auto result = store.advance(current, next, verify, scratch, capacity);
    if (result == PublicationStoreResult::Ready) current = next;
    return result;
}

template <class Store>
PublicationStoreResult persistTrackUploadSealed(Store& store, PublicationJournal& current,
                                                PublicationJournal& next, PublicationJournal& verify,
                                                geocaching::ByteView response, geocaching::ByteView request,
                                                uint8_t* scratch, size_t capacity)
{
    if (!uploadJournalWorkspace(current, next, verify, response, request, {}, scratch, capacity) ||
        (current.phase != PublicationPhase::Uploading && current.phase != PublicationPhase::FileStored) ||
        current.offset != current.file_bytes ||
        !decodeTrackUploadSealed(response, request, {current.file_hash, 32}, current.file_bytes)) return PublicationStoreResult::Invalid;
    if (current.phase == PublicationPhase::FileStored) return store.validateCurrent(current, verify, scratch, capacity);
    if (current.sequence == UINT64_MAX) return PublicationStoreResult::Invalid;
    next = current;
    next.phase = PublicationPhase::FileStored;
    ++next.sequence;
    const auto result = store.advance(current, next, verify, scratch, capacity);
    if (result == PublicationStoreResult::Ready) current = next;
    return result;
}
} // namespace track_sharing
