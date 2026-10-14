#pragma once
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "track_sharing/publication_slot_path.h"
namespace hostlink::management::publication_runtime
{
enum class HistoryScanResult : uint8_t { Pending, Candidate, End, Busy, Unavailable, Invalid, StorageFailure };
// Caller admits scanner and one PsramOnly directory in PSRAM. No list or open
// handle survives a step. Candidates still require journal/signature validation.
class HistoryScan
{
    using Dir = platform::esp::arduino_common::storage::SdRuntimeDir;
    static constexpr char prefix_[]="/trailmate/tracks/";
    static constexpr size_t prefix_bytes_=sizeof(prefix_)-1, object_bytes_=prefix_bytes_+64;
  public:
    HistoryScan(Dir& directory, uint32_t media) : directory_(directory), media_(media) {}
    HistoryScanResult step(track_sharing::PublicationSlotKey& output)
    {
        namespace sd = platform::esp::arduino_common::storage;
        if (media_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return HistoryScanResult::Unavailable;
        if (!directory_.has_backing_storage()) return HistoryScanResult::Unavailable;
        if (ended_) return HistoryScanResult::End;
        const bool inside = object_path_[0] != 0;
        const auto opened = directory_.open_read_status(inside ? object_path_ : track_sharing::kObjectDirectory, media_);
        if (opened != sd::SdFileReadStatus::Ready)
        {
            directory_.close();
            if (opened == sd::SdFileReadStatus::Busy) return HistoryScanResult::Busy;
            if (opened == sd::SdFileReadStatus::Missing)
            {
                if (inside) { object_path_[0]=0; file_position_=0; return HistoryScanResult::Pending; }
                ended_=true;return HistoryScanResult::End;
            }
            return opened == sd::SdFileReadStatus::Unavailable ? HistoryScanResult::Unavailable : HistoryScanResult::StorageFailure;
        }
        auto& position = inside ? file_position_ : root_position_;
        const auto sought = directory_.seek_read_status(position);
        if (sought != sd::SdFileReadStatus::Ready)
        {
            directory_.close();
            return sought == sd::SdFileReadStatus::Busy ? HistoryScanResult::Busy : sought == sd::SdFileReadStatus::Unavailable ? HistoryScanResult::Unavailable : HistoryScanResult::StorageFailure;
        }
        bool is_directory=false;
        uint64_t bytes=0,next=position;
        const auto read=directory_.read_next_info(name_,sizeof(name_),&is_directory,&bytes,&next);
        directory_.close();
        if (media_ != sd::sd_media_session()) return HistoryScanResult::Unavailable;
        if (read == sd::SdDirReadStatus::Busy) return HistoryScanResult::Busy;
        if (read == sd::SdDirReadStatus::End)
        {
            if(inside){object_path_[0]=0;file_position_=0;return HistoryScanResult::Pending;}
            ended_=true;return HistoryScanResult::End;
        }
        if(read != sd::SdDirReadStatus::Entry) return read == sd::SdDirReadStatus::Unavailable ? HistoryScanResult::Unavailable : HistoryScanResult::StorageFailure;
        if(next<=position)return HistoryScanResult::Invalid;
        position=next;
        if(!inside)
        {
            if(!is_directory || std::strlen(name_)!=64)return HistoryScanResult::Pending;
            unsigned bits=0;
            for(unsigned i=0;i<64;++i){const char c=name_[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return HistoryScanResult::Pending;bits|=c!='0';}
            if(!bits)return HistoryScanResult::Pending;
            std::memcpy(object_path_,prefix_,prefix_bytes_);std::memcpy(object_path_+prefix_bytes_,name_,65);
            return HistoryScanResult::Pending;
        }
        if(is_directory || std::strlen(name_)!=36)return HistoryScanResult::Pending;
        std::memcpy(path_,object_path_,object_bytes_);path_[object_bytes_]='/';std::memcpy(path_+object_bytes_+1,name_,37);
        return track_sharing::parsePublicationSlotPath(path_,object_bytes_+37,output) ? HistoryScanResult::Candidate : HistoryScanResult::Pending;
    }
  private:
    Dir& directory_;
    uint32_t media_;
    uint64_t root_position_=0,file_position_=0;
    char object_path_[object_bytes_+1]{},path_[object_bytes_+38]{},name_[65]{};
    bool ended_=false;
};
static_assert(sizeof(HistoryScan)<=320,"History scanner cannot own a file list");
}
