#pragma once
#include "track_sharing/upload_journal.h"
#include "track_sharing/sign_manifest.h"
#include <initializer_list>
namespace track_sharing
{
enum class PublicationPhase : uint8_t { Accepted, Uploading, FileStored, Publishing, Confirmed, Failed, Cancelled };
struct PublicationJournal
{
    uint8_t operation[16]{}, object[32]{}, revision_hash[32]{}, directory[16]{}, directory_epoch[16]{}, file_hash[32]{}, author_hash[32]{};
    uint64_t sequence = 0;
    uint32_t generation = 0, revision = 0, file_bytes = 0, offset = 0;
    PublicationPhase phase = PublicationPhase::Accepted;
};
static_assert(sizeof(PublicationJournal) <= 216, "Publication metadata cannot own KML bytes");
constexpr size_t kPublicationJournalBytes = 208;
inline bool publicationJournalValid(const PublicationJournal& value)
{
    for (const auto field : {geocaching::ByteView{value.operation,16},{value.object,32},{value.revision_hash,32},{value.directory,16},{value.directory_epoch,16},{value.file_hash,32},{value.author_hash,32}})
    { uint8_t bits=0;for(size_t i=0;i<field.size;++i)bits|=field.data[i];if(!bits)return false; }
    return value.sequence && value.generation && value.revision && value.file_bytes && value.file_bytes <= 8388608 && value.offset <= value.file_bytes &&
        static_cast<uint8_t>(value.phase) <= 6 && (value.phase != PublicationPhase::Accepted || !value.offset) &&
        ((value.phase != PublicationPhase::FileStored && value.phase != PublicationPhase::Publishing && value.phase != PublicationPhase::Confirmed) || value.offset == value.file_bytes);
}
// CRC is damage detection only. Runtime authenticates author, version, blob and
// directory; Confirmed additionally requires a verified directory receipt.
inline bool publicationAdvanceValid(const PublicationJournal& current, const PublicationJournal& next)
{
    if (!publicationJournalValid(current) || !publicationJournalValid(next) || current.sequence == UINT64_MAX ||
        next.sequence != current.sequence + 1 || current.generation != next.generation || current.revision != next.revision ||
        current.file_bytes != next.file_bytes || next.offset < current.offset ||
        std::memcmp(current.operation,next.operation,16) || std::memcmp(current.object,next.object,32) ||
        std::memcmp(current.revision_hash,next.revision_hash,32) || std::memcmp(current.directory,next.directory,16) ||
        std::memcmp(current.directory_epoch,next.directory_epoch,16) || std::memcmp(current.file_hash,next.file_hash,32) ||
        std::memcmp(current.author_hash,next.author_hash,32)) return false;
    const bool terminal = current.phase == PublicationPhase::Confirmed || current.phase == PublicationPhase::Failed || current.phase == PublicationPhase::Cancelled;
    if (terminal) return next.phase == current.phase && next.offset == current.offset;
    if (next.phase == current.phase) return current.phase == PublicationPhase::Uploading || next.offset == current.offset;
    if (next.phase == PublicationPhase::Failed) return next.offset == current.offset;
    if (next.phase == PublicationPhase::Cancelled) return current.phase != PublicationPhase::Publishing && next.offset == current.offset;
    if (current.phase == PublicationPhase::Accepted) return next.phase == PublicationPhase::Uploading || next.phase == PublicationPhase::FileStored;
    if (current.phase == PublicationPhase::Uploading) return next.phase == PublicationPhase::FileStored;
    if (current.phase == PublicationPhase::FileStored) return next.phase == PublicationPhase::Publishing;
    return current.phase == PublicationPhase::Publishing && next.phase == PublicationPhase::Confirmed;
}
inline bool encodePublicationJournal(const PublicationJournal& value, uint8_t* output, size_t capacity)
{
    if (!output || capacity < kPublicationJournalBytes || !publicationJournalValid(value) ||
        signingSpansOverlap(output,kPublicationJournalBytes,reinterpret_cast<const uint8_t*>(&value),sizeof(value)))return false;
    std::memcpy(output,"TMTP",4);output[4]=1;size_t pos=5;
    for(const auto field:{geocaching::ByteView{value.operation,16},{value.object,32},{value.revision_hash,32},{value.directory,16},{value.directory_epoch,16},{value.file_hash,32},{value.author_hash,32}})
    {std::memcpy(output+pos,field.data,field.size);pos+=field.size;}
    for(const auto number:{value.generation,value.revision,value.file_bytes,value.offset})
    {for(unsigned i=0;i<4;++i)output[pos++]=uint8_t(number>>(8*i));}
    for(unsigned i=0;i<8;++i)output[pos++]=uint8_t(value.sequence>>(8*i));
    output[pos++]=uint8_t(value.phase);const auto crc=uploadJournalCrc(output,pos);output[pos++]=uint8_t(crc);output[pos]=uint8_t(crc>>8);return true;
}
inline bool decodePublicationJournal(const uint8_t* input,size_t size,PublicationJournal& value)
{
    if(input&&signingSpansOverlap(input,size,reinterpret_cast<const uint8_t*>(&value),sizeof(value)))return false;
    value={};if(!input||size!=kPublicationJournalBytes||std::memcmp(input,"TMTP",4)||input[4]!=1||
        uploadJournalCrc(input,206)!=(uint16_t(input[206])|(uint16_t(input[207])<<8)))return false;
    size_t pos=5;
    for(const auto field:{geocaching::ByteView{value.operation,16},{value.object,32},{value.revision_hash,32},{value.directory,16},{value.directory_epoch,16},{value.file_hash,32},{value.author_hash,32}})
    {std::memcpy(const_cast<uint8_t*>(field.data),input+pos,field.size);pos+=field.size;}
    for(auto* number:{&value.generation,&value.revision,&value.file_bytes,&value.offset})
    {for(unsigned i=0;i<4;++i)*number|=uint32_t(input[pos++])<<(8*i);}
    for(unsigned i=0;i<8;++i)value.sequence|=uint64_t(input[pos++])<<(8*i);
    value.phase=static_cast<PublicationPhase>(input[pos]);return publicationJournalValid(value);
}
} // namespace track_sharing
