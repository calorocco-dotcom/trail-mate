#pragma once
#include "track_sharing/publication_journal_store.h"
namespace track_sharing
{
inline bool publicationBindingsEqual(const PublicationJournal& a, const PublicationJournal& b)
{
    return a.generation==b.generation && a.revision==b.revision && a.file_bytes==b.file_bytes &&
        !std::memcmp(a.operation,b.operation,16) && !std::memcmp(a.object,b.object,32) && !std::memcmp(a.revision_hash,b.revision_hash,32) &&
        !std::memcmp(a.directory,b.directory,16) && !std::memcmp(a.directory_epoch,b.directory_epoch,16) &&
        !std::memcmp(a.file_hash,b.file_hash,32) && !std::memcmp(a.author_hash,b.author_hash,32);
}
// Caller constructs desired from authenticated intent evidence and holds an
// exclusive media/path lease. Repeating admission never resets network progress.
template <class Store>
PublicationStoreResult persistPublicationIntent(Store& store,const PublicationJournal& desired,
                                               PublicationJournal& current,PublicationJournal& verify,uint8_t* scratch,size_t capacity)
{
    if(&desired==&current || &desired==&verify || &current==&verify || !publicationJournalValid(desired) ||
       desired.sequence!=1 || desired.phase!=PublicationPhase::Accepted || desired.offset) return PublicationStoreResult::Invalid;
    const auto loaded=store.load(current,verify,scratch,capacity);
    if(loaded==PublicationStoreResult::Ready)return publicationBindingsEqual(desired,current)?PublicationStoreResult::Ready:PublicationStoreResult::Conflict;
    if(loaded!=PublicationStoreResult::Missing)return loaded;
    const auto result=store.initialize(desired,verify,scratch,capacity);
    if(result==PublicationStoreResult::Ready)current=desired;
    return result;
}
} // namespace track_sharing
