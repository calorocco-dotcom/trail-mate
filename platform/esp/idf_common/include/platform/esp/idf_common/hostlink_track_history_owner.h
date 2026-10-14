#pragma once
#include "platform/esp/idf_common/hostlink_track_history_query.h"
#include "track_sharing/publication_candidate_reader.h"
namespace hostlink::management::publication_runtime
{
class HistoryOwner
{
  public:
    HistoryOwner(geocaching::ByteView operation,uint32_t generation,const uint8_t* session,uint32_t media)
        :file_(sd::SdRuntimeMemoryPolicy::PsramOnly),directory_(sd::SdRuntimeMemoryPolicy::PsramOnly),io_(file_,media),
        reader_(io_),scan_(directory_,media),query_(scan_,reader_,operation,generation,media),generation_(generation)
    {std::memcpy(operation_,operation.data,16);std::memcpy(session_,session,16);}
    bool backed()const{return file_.has_backing_storage() && directory_.has_backing_storage();}
    bool matches(geocaching::ByteView operation,uint32_t generation,const uint8_t* session)const
    {return session && operation.data && operation.size==16 && generation==generation_ && !std::memcmp(operation_,operation.data,16) && !std::memcmp(session_,session,16);}
    bool matchesSession(const uint8_t* session)const{return session && !std::memcmp(session_,session,16);}
    Status step(chat::IMeshAdapter& adapter,uint8_t* scratch,size_t capacity){return status_=query_.step(adapter,scratch,capacity);}
    void advance(chat::IMeshAdapter& adapter,uint8_t* scratch,size_t capacity)
    {if(status_==Status::Busy)step(adapter,scratch,capacity);}
    const track_sharing::PublicationJournal* result()const{return query_.result();}
  private:
    sd::SdRuntimeFile file_;
    sd::SdRuntimeDir directory_;
    sd::SdBorrowedRecordFileIo io_;
    track_sharing::PublicationCandidateReader<sd::SdBorrowedRecordFileIo> reader_;
    HistoryScan scan_;
    HistoryQuery<HistoryScan,decltype(reader_)> query_;
    uint8_t operation_[16]{},session_[16]{};
    uint32_t generation_;
    Status status_=Status::Busy;
};
static_assert(sizeof(HistoryOwner)<=2048,"History lookup cannot retain a file list");
inline void releaseHistory(HistoryOwner*& owner)
{if(owner){owner->~HistoryOwner();heap_caps_free(owner);owner=nullptr;}}
inline HistoryOwner* createHistory(geocaching::ByteView operation,uint32_t generation,const uint8_t* session)
{
    if(!operation.data || operation.size!=16 || !generation || !session || !sd::sd_card_ready() || sd::sd_external_block_owner_active())return nullptr;
    void* memory=heap_caps_malloc(sizeof(HistoryOwner),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory)return nullptr;
    auto* result=new(memory) HistoryOwner(operation,generation,session,sd::sd_media_session());
    if(result->backed())return result;
    releaseHistory(result);return nullptr;
}
}
