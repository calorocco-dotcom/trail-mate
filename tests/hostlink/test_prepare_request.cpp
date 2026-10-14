#include "track_sharing/prepare_request.h"
#include "track_sharing/commit_request.h"
#include "track_sharing/publication_journal.h"
#include "track_sharing/receipt.h"
#include "geocaching/protocol/cmp_writer.h"
#include <cassert>
int main()
{
    uint8_t raw[140], operation[16] = {1}, kml[16] = {2}, manifest[16] = {3}, parent[32] = {4}, zero[32]{};
    track_sharing::PrepareRequest output;
    {
        uint8_t receipt_raw[512],public_key[64]={7};
        geocaching::protocol::CmpWriter writer(receipt_raw,sizeof(receipt_raw));
        assert(writer.array(13)&&writer.unsignedInteger(1)&&writer.binary({operation,16})&&writer.binary({parent,32})&&writer.binary({parent,32})&&
               writer.unsignedInteger(2)&&writer.unsignedInteger(0)&&writer.binary({parent,32})&&writer.unsignedInteger(750)&&
               writer.binary({public_key,64})&&writer.binary({kml,16})&&writer.binary({manifest,16})&&writer.unsignedInteger(1)&&writer.unsignedInteger(5000000000ULL));
        track_sharing::ReceiptView receipt;
        assert(track_sharing::decodeReceipt({receipt_raw,writer.size()},receipt)&&receipt.accepted==5000000000ULL&&receipt.file_bytes==750);
        assert(receipt.raw.data==receipt_raw&&receipt.directory_public.size==64);
        assert(!track_sharing::decodeReceipt({receipt_raw,writer.size()-1},receipt));
        receipt_raw[writer.size()]=0;
        assert(!track_sharing::decodeReceipt({receipt_raw,writer.size()+1},receipt));
    }
    {
        track_sharing::PublicationJournal job, restored;
        for (const auto field : {geocaching::ByteView{job.operation,16},{job.object,32},{job.revision_hash,32},{job.directory,16},{job.directory_epoch,16},{job.file_hash,32},{job.author_hash,32}})std::memset(const_cast<uint8_t*>(field.data),1,field.size);
        job.sequence=1;job.generation=1;job.revision=1;job.file_bytes=750;
        uint8_t journal[track_sharing::kPublicationJournalBytes];
        assert(track_sharing::encodePublicationJournal(job,journal,sizeof(journal)) && track_sharing::decodePublicationJournal(journal,sizeof(journal),restored));
        assert(restored.sequence==1&&restored.file_bytes==750&&restored.phase==track_sharing::PublicationPhase::Accepted);
        for(size_t i=0;i<sizeof(journal);++i){journal[i]^=1;assert(!track_sharing::decodePublicationJournal(journal,sizeof(journal),restored));journal[i]^=1;}
        job.phase=track_sharing::PublicationPhase::Confirmed;
        assert(!track_sharing::encodePublicationJournal(job,journal,sizeof(journal)));
        job.offset=750;assert(track_sharing::encodePublicationJournal(job,journal,sizeof(journal)));
        job.phase=track_sharing::PublicationPhase::Accepted;job.offset=0;
        auto next=job;next.sequence=2;next.phase=track_sharing::PublicationPhase::Uploading;next.offset=50;
        assert(track_sharing::publicationAdvanceValid(job,next));
        next.directory_epoch[0]^=1;assert(!track_sharing::publicationAdvanceValid(job,next));next.directory_epoch[0]^=1;
        job=next;next.sequence++;next.offset=49;assert(!track_sharing::publicationAdvanceValid(job,next));
        next.offset=750;next.phase=track_sharing::PublicationPhase::FileStored;assert(track_sharing::publicationAdvanceValid(job,next));
        job=next;next.sequence++;next.phase=track_sharing::PublicationPhase::Confirmed;assert(!track_sharing::publicationAdvanceValid(job,next));
        next.phase=track_sharing::PublicationPhase::Publishing;assert(track_sharing::publicationAdvanceValid(job,next));
        job=next;next.sequence++;next.phase=track_sharing::PublicationPhase::Cancelled;assert(!track_sharing::publicationAdvanceValid(job,next));
        next.phase=track_sharing::PublicationPhase::Confirmed;assert(track_sharing::publicationAdvanceValid(job,next));
        job=next;next.sequence++;next.phase=track_sharing::PublicationPhase::Uploading;assert(!track_sharing::publicationAdvanceValid(job,next));
        job.sequence=UINT64_MAX;next=job;next.sequence=1;assert(!track_sharing::publicationAdvanceValid(job,next));
    }
    {
        track_sharing::CommitRequest commit;
        for (unsigned mode = 0; mode < 6; ++mode)
        {
            geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
            assert(writer.array(7) && writer.binary({operation, 16}) && writer.binary({parent, 32}) && writer.binary({mode == 3 ? zero : parent, 32}) &&
                   writer.unsignedInteger(mode == 1 ? 0 : UINT32_MAX) && writer.unsignedInteger(mode == 2 ? uint64_t(UINT32_MAX) + 1 : UINT32_MAX) &&
                   writer.binary({mode == 4 ? zero : kml, 16}) && writer.binary({mode == 5 ? zero : manifest, 16}));
            const auto length = writer.size();
            assert(track_sharing::decodeCommitRequest({raw, length}, commit) == (mode == 0));
            if (mode == 0)
            {
                assert(length == 133 && commit.generation == UINT32_MAX && commit.revision == UINT32_MAX);
                assert(!track_sharing::decodeCommitRequest({raw, length - 1}, commit));
                raw[length] = 0;assert(!track_sharing::decodeCommitRequest({raw, length + 1}, commit));
            }
        }
    }
    for (unsigned mode = 0; mode < 8; ++mode)
    {
        geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
        assert(writer.array(5) && writer.binary({mode == 4 ? zero : operation, 16}) && writer.binary({kml, 16}) &&
               writer.binary({mode == 3 ? kml : manifest, 16}) && writer.unsignedInteger(mode == 2 ? 0 : mode == 6 ? uint64_t(UINT32_MAX) + 1 : UINT32_MAX));
        assert(mode == 0 || mode == 7 ? writer.nil() : writer.binary({mode == 5 ? zero : parent, 32}));
        const auto length = writer.size();
        const bool valid = mode == 0 || mode == 1 || mode == 7;
        assert(track_sharing::decodePrepareRequest({raw, length}, output) == valid);
        if (valid)
        {
            assert(output.generation == UINT32_MAX && output.kml_upload.data == raw + 21);
            assert(output.expected_parent.size == (mode == 1 ? 32 : 0));
            assert(!track_sharing::decodePrepareRequest({raw, length - 1}, output));
            raw[length] = 0;
            assert(!track_sharing::decodePrepareRequest({raw, length + 1}, output));
        }
    }
}
