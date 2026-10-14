#pragma once
#include "track_sharing/publication_journal_store.h"
namespace track_sharing
{
// Caller holds current identity, media and exclusive callback/path serialization.
// Cancelling local upload is not withdrawal of an already sent publication.
template<class Jobs>
PublicationStoreResult cancelPublicationBeforePublish(Jobs& jobs,PublicationJournal& current,
    PublicationJournal& next,PublicationJournal& verify,uint32_t generation,geocaching::ByteView author_hash,
    uint8_t* scratch,size_t capacity)
{
    if(&current==&next || &current==&verify || &next==&verify || !publicationJournalValid(current) ||
        !generation || !author_hash.data || author_hash.size!=32 || !scratch || capacity<kPublicationJournalBytes)
        return PublicationStoreResult::Invalid;
    for(const auto record:{geocaching::ByteView{reinterpret_cast<const uint8_t*>(&current),sizeof(current)},
        {reinterpret_cast<const uint8_t*>(&next),sizeof(next)},{reinterpret_cast<const uint8_t*>(&verify),sizeof(verify)}})
        if(signingSpansOverlap(record.data,record.size,scratch,capacity) || signingSpansOverlap(record.data,record.size,author_hash.data,author_hash.size))
            return PublicationStoreResult::Invalid;
    if(generation!=current.generation || std::memcmp(author_hash.data,current.author_hash,32))return PublicationStoreResult::Conflict;
    if(current.phase==PublicationPhase::Cancelled)return jobs.validateCurrent(current,verify,scratch,capacity);
    if(current.phase!=PublicationPhase::Accepted && current.phase!=PublicationPhase::Uploading && current.phase!=PublicationPhase::FileStored)
        return PublicationStoreResult::Conflict;
    if(current.sequence==UINT64_MAX)return PublicationStoreResult::Conflict;
    next=current;++next.sequence;next.phase=PublicationPhase::Cancelled;
    const auto written=jobs.advance(current,next,verify,scratch,capacity);
    if(written==PublicationStoreResult::Ready)current=next;
    return written;
}
}
