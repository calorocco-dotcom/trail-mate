#pragma once
#include "track_sharing/verify_receipt.h"
#include "track_sharing/publication_journal_store.h"
#include "track_sharing/signed_manifest_store.h"
namespace track_sharing
{
// Caller keeps verified receipt bytes immutable, expected directory provenance,
// author/media and exclusive canonical paths valid through both durable writes.
template<class Jobs,class Receipts>
PublicationStoreResult confirmPublication(Jobs& jobs,Receipts& receipts,const VerifiedReceiptView& proof,
                                         const PublicationJournal& current,PublicationJournal& next,PublicationJournal& verify,
                                         geocaching::ByteView request,geocaching::ByteView directory_public,uint8_t state,
                                         uint8_t* scratch,size_t capacity)
{
    if(&current==&next||&current==&verify||&next==&verify||!proof.verified||!proof.signature.data||proof.signature.size!=64||
       !receiptMatchesJob(proof.receipt,current,request,directory_public,state)||current.offset!=current.file_bytes||
       (current.phase!=PublicationPhase::Publishing&&current.phase!=PublicationPhase::Confirmed))return PublicationStoreResult::Invalid;
    if(current.phase!=PublicationPhase::Confirmed&&current.sequence==UINT64_MAX)return PublicationStoreResult::Conflict;
    const auto valid=jobs.validateCurrent(current,verify,scratch,capacity);
    if(valid!=PublicationStoreResult::Ready)return valid;
    const auto persisted=receipts.persist(proof.receipt.raw,proof.signature,scratch,capacity);
    if(persisted!=SignedStoreResult::Ready)return persisted==SignedStoreResult::StorageFailure?PublicationStoreResult::StorageFailure:PublicationStoreResult::Conflict;
    if(current.phase==PublicationPhase::Confirmed)return PublicationStoreResult::Ready;
    next=current;++next.sequence;next.phase=PublicationPhase::Confirmed;
    return jobs.advance(current,next,verify,scratch,capacity);
}
} // namespace track_sharing
