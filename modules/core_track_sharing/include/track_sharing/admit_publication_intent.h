#pragma once
#include "track_sharing/persist_publication_intent.h"
#include "track_sharing/publication_directory_store.h"

namespace track_sharing
{
// Caller has authenticated signed-head/blob/current identity evidence. A pin
// survives USB-owner retirement and must be durable before the job journal.
// Crash between these writes leaves only an immutable pin; identical admission
// can resume, while retargeting the same operation fails closed.
template <class Directories, class Jobs>
PublicationStoreResult admitPublicationIntent(Directories& directories, Jobs& jobs,
                                              const PublicationDirectory& directory, PublicationDirectory& directory_verify,
                                              const PublicationJournal& desired, PublicationJournal& current,
                                              PublicationJournal& verify, uint8_t* scratch, size_t capacity)
{
    const auto disjoint = [](const void* a, size_t a_size, const void* b, size_t b_size)
    {
        return !signingSpansOverlap(reinterpret_cast<const uint8_t*>(a), a_size,
                                    reinterpret_cast<const uint8_t*>(b), b_size);
    };
    const void* records[] = {&directory, &directory_verify, &desired, &current, &verify};
    const size_t sizes[] = {sizeof(directory), sizeof(directory_verify), sizeof(desired), sizeof(current), sizeof(verify)};
    if (!scratch || capacity < kPublicationJournalBytes || !publicationDirectoryMatches(directory, desired) ||
        desired.sequence != 1 || desired.phase != PublicationPhase::Accepted || desired.offset) return PublicationStoreResult::Invalid;
    for (size_t i = 0; i < 5; ++i)
    {
        if (!disjoint(records[i], sizes[i], scratch, capacity)) return PublicationStoreResult::Invalid;
        for (size_t j = i + 1; j < 5; ++j)
            if (!disjoint(records[i], sizes[i], records[j], sizes[j])) return PublicationStoreResult::Invalid;
    }
    const auto pinned = directories.admit(directory, directory_verify, scratch, capacity);
    if (pinned != PublicationStoreResult::Ready) return pinned;
    if (!publicationDirectoryMatches(directory_verify, desired) || !publicationDirectoriesEqual(directory, directory_verify)) return PublicationStoreResult::Conflict;
    return persistPublicationIntent(jobs, desired, current, verify, scratch, capacity);
}
} // namespace track_sharing
