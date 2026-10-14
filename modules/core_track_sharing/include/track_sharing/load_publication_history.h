#pragma once
#include "track_sharing/admit_publication_intent.h"
namespace track_sharing
{
// Read-only recovery. Caller reconstructs desired from fresh signed manifest,
// blob/current-author evidence and holds the media lease. Ready identifies the
// journal only: Confirmed/Publishing still require PublicationReceiptLoader.
template <class Directories, class Jobs>
PublicationStoreResult loadPublicationHistory(Directories& directories, Jobs& jobs,
                                              const PublicationDirectory& expected_directory, const PublicationJournal& authenticated_intent,
                                              PublicationDirectory& directory, PublicationJournal& current, PublicationJournal& other,
                                              uint8_t* scratch, size_t capacity)
{
    const void* records[] = {&expected_directory, &authenticated_intent, &directory, &current, &other};
    const size_t sizes[] = {sizeof(expected_directory), sizeof(authenticated_intent), sizeof(directory), sizeof(current), sizeof(other)};
    if (!scratch || capacity < kPublicationJournalBytes ||
        !publicationDirectoryMatches(expected_directory, authenticated_intent) ||
        authenticated_intent.sequence != 1 || authenticated_intent.phase != PublicationPhase::Accepted || authenticated_intent.offset)
        return PublicationStoreResult::Invalid;
    for (size_t i = 0; i < 5; ++i)
    {
        const auto* input = reinterpret_cast<const uint8_t*>(records[i]);
        if (signingSpansOverlap(input, sizes[i], scratch, capacity)) return PublicationStoreResult::Invalid;
        for (size_t j = i + 1; j < 5; ++j)
            if (signingSpansOverlap(input, sizes[i], reinterpret_cast<const uint8_t*>(records[j]), sizes[j])) return PublicationStoreResult::Invalid;
    }
    const auto fail = [&](PublicationStoreResult result)
    {directory={};current={};other={};return result; };
    const auto pin = directories.load(directory, scratch, capacity);
    if (pin != PublicationStoreResult::Ready) return fail(pin);
    if (!publicationDirectoriesEqual(expected_directory, directory)) return fail(PublicationStoreResult::Conflict);
    const auto loaded = jobs.load(current, other, scratch, capacity);
    if (loaded != PublicationStoreResult::Ready) return fail(loaded);
    if (!publicationBindingsEqual(authenticated_intent, current) || !publicationDirectoryMatches(directory, current))
        return fail(PublicationStoreResult::Conflict);
    return PublicationStoreResult::Ready;
}
} // namespace track_sharing
