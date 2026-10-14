#pragma once
#include "track_sharing/persist_signed_head.h"
#include "track_sharing/publication_journal_store.h"
namespace track_sharing
{
// Durable task admission precedes Committed head. Neither establishes a remote
// receipt. Caller retains verified manifest/reservation, current identity/media
// and exclusive object paths; records and scratch are disjoint.
template <class Heads,class Jobs>
HeadStoreResult commitPublicationHead(Heads& heads,Jobs& jobs,const PublicationJournal& job,PublicationJournal& job_verify,
                                     const VerifiedManifestView& signed_view,const RevisionReservation& reservation,
                                     uint32_t generation,geocaching::ByteView author,geocaching::ByteView author_hash,
                                     ObjectHead& current,ObjectHead& next,ObjectHead& verify,uint8_t* scratch,size_t capacity)
{
    if(&current==&next||&current==&verify||&next==&verify||&job==&job_verify||!publicationJournalValid(job)||
       !author_hash.data||author_hash.size!=32||job.generation!=generation||!signed_view.verified||
       job.revision!=signed_view.manifest.revision||job.file_bytes!=signed_view.manifest.file_bytes||
       signed_view.manifest.file_hash.size!=32||!signed_view.manifest.file_hash.data||
       std::memcmp(job.object,signed_view.object_id,32)||std::memcmp(job.revision_hash,signed_view.revision_hash,32)||
       std::memcmp(job.file_hash,signed_view.manifest.file_hash.data,32)||std::memcmp(job.author_hash,author_hash.data,32))return HeadStoreResult::Invalid;
    const auto loaded=heads.load(current,verify,scratch,capacity);
    if(loaded!=HeadStoreResult::Ready)return loaded;
    if(std::memcmp(job.operation,current.operation,16)||!signedHeadMatches(current,signed_view,reservation,generation,author,author_hash))return HeadStoreResult::Conflict;
    const auto durable=jobs.validateCurrent(job,job_verify,scratch,capacity);
    if(durable!=PublicationStoreResult::Ready)return durable==PublicationStoreResult::StorageFailure?HeadStoreResult::StorageFailure:HeadStoreResult::Conflict;
    if(current.phase==HeadPhase::Committed)return HeadStoreResult::Ready;
    if(current.sequence==UINT64_MAX)return HeadStoreResult::Conflict;
    next=current;++next.sequence;next.phase=HeadPhase::Committed;
    const auto result=heads.advance(current,next,verify,scratch,capacity);
    if(result==HeadStoreResult::Ready)current=next;
    return result;
}
} // namespace track_sharing
