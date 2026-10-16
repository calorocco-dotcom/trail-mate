#include "music/storage.h"
#include "platform/esp/common/music/music_ports.h"
#include <algorithm>
#include <cstring>
namespace platform::esp::music_runtime
{
using ::music::StorageResult;
SdStorage::SdStorage()
{
    for (auto& handle : handles_) handle.owner = this;
}
SdStorage::~SdStorage()
{
    directory_.close();
    for (auto& handle : handles_) handle.file.close();
}
bool SdStorage::ready() const { return sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
uint32_t SdStorage::session() const { return sd::sd_media_session(); }
uint32_t SdStorage::Handle::size() const
{
    const auto bytes = file.size();
    if (file.read_busy()) owner->result_ = StorageResult::Busy;
    return bytes > UINT32_MAX ? 0 : static_cast<uint32_t>(bytes);
}
std::size_t SdStorage::Handle::read_at(uint32_t offset, void* out, std::size_t size)
{
    if (!file.seek(offset))
    {
        owner->result_ = file.read_busy() ? StorageResult::Busy : StorageResult::Error;
        return 0;
    }
    const int count = file.read(out, size);
    owner->result_ = count > 0 ? StorageResult::Ok : file.read_busy() ? StorageResult::Busy
                                                                      : StorageResult::Error;
    return count > 0 ? count : 0;
}
std::size_t SdStorage::Handle::write(const void* input, std::size_t size)
{
    const auto written = file.write(input, size);
    owner->result_ = written == size ? StorageResult::Ok : StorageResult::Error;
    return written;
}
bool SdStorage::Handle::flush() { return file.flush(); }
::music::File* SdStorage::open(const char* path, ::music::FileMode mode)
{
    if (!ready())
    {
        result_ = StorageResult::Unavailable;
        return nullptr;
    }
    for (auto& handle : handles_)
        if (!handle.used)
        {
            const char* flags = mode == ::music::FileMode::Read ? "r" : mode == ::music::FileMode::Write ? "w"
                                                                                                         : "a";
            if (!handle.file.open(path, flags, session()))
            {
                result_ = handle.file.read_busy() ? StorageResult::Busy : StorageResult::Missing;
                return nullptr;
            }
            handle.used = true;
            result_ = StorageResult::Ok;
            return &handle;
        }
    result_ = StorageResult::Busy;
    return nullptr;
}
void SdStorage::close(::music::File*& file)
{
    if (!file) return;
    auto* handle = static_cast<Handle*>(file);
    handle->file.close();
    handle->used = false;
    file = nullptr;
}
StorageResult SdStorage::next(const char* path, uint64_t& cursor, ::music::Entry& entry)
{
    if (!ready()) return result_ = StorageResult::Unavailable;
    if (!directory_.is_open() || directory_session_ != session() || std::strcmp(directory_path_, path))
    {
        directory_.close();
        const auto status = directory_.open_read_status(path, session());
        if (status != sd::SdFileReadStatus::Ready) return result_ = status == sd::SdFileReadStatus::Busy ? StorageResult::Busy : status == sd::SdFileReadStatus::Missing ? StorageResult::Missing
                                                                                                                                                                         : StorageResult::Error;
        ::music::copy_text(directory_path_, sizeof(directory_path_), path);
        directory_session_ = session();
    }
    const auto seek = directory_.seek_read_status(cursor);
    if (seek != sd::SdFileReadStatus::Ready) return result_ = seek == sd::SdFileReadStatus::Busy ? StorageResult::Busy : StorageResult::Error;
    uint64_t next = cursor;
    const auto status = directory_.read_next_info(entry.name, sizeof(entry.name), &entry.directory, &entry.size, &next);
    if (status == sd::SdDirReadStatus::Entry)
    {
        cursor = next;
        return result_ = StorageResult::Ok;
    }
    directory_.close();
    directory_path_[0] = 0;
    directory_session_ = 0;
    return result_ = status == sd::SdDirReadStatus::End ? StorageResult::End : status == sd::SdDirReadStatus::Busy      ? StorageResult::Busy
                                                                           : status == sd::SdDirReadStatus::Unavailable ? StorageResult::Unavailable
                                                                                                                        : StorageResult::Error;
}
bool SdStorage::exists(const char* path) { return ready() && sd::sd_exists(path); }
bool SdStorage::mkdir(const char* path) { return ready() && (sd::sd_is_directory(path) || sd::sd_mkdir(path, session())); }
bool SdStorage::remove(const char* path) { return ready() && sd::sd_remove(path, session()); }
bool SdStorage::rename(const char* from, const char* to) { return ready() && sd::sd_rename(from, to, session()); }
} // namespace platform::esp::music_runtime
