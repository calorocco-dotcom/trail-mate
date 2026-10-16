#pragma once
#include "lvgl_draw.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
using lv_fs_res_t = unsigned;
constexpr lv_fs_res_t LV_FS_RES_OK = 0, LV_FS_RES_UNKNOWN = 1;
constexpr unsigned LV_FS_MODE_RD = 1;
struct lv_fs_file_t
{
    const std::string* content = nullptr;
    size_t offset = 0;
    bool open = false;
};
struct lv_fs_dir_t
{
};
namespace kml_reader_fake
{
inline std::map<std::string, std::string> files;
inline unsigned open_handles = 0, reads = 0;
inline int fail_read = -1;
inline bool close_fail = false;
} // namespace kml_reader_fake
inline lv_fs_res_t lv_fs_open(lv_fs_file_t* file, const char* path, unsigned)
{
    auto found = kml_reader_fake::files.find(path);
    if (found == kml_reader_fake::files.end()) return LV_FS_RES_UNKNOWN;
    *file = {&found->second, 0, true};
    ++kml_reader_fake::open_handles;
    return LV_FS_RES_OK;
}
inline lv_fs_res_t lv_fs_read(lv_fs_file_t* file, void* output, uint32_t capacity, uint32_t* count)
{
    ++kml_reader_fake::reads;
    *count = 0;
    if (!file->open || static_cast<int>(kml_reader_fake::reads) == kml_reader_fake::fail_read) return LV_FS_RES_UNKNOWN;
    *count = static_cast<uint32_t>(std::min<size_t>(capacity, file->content->size() - file->offset));
    std::memcpy(output, file->content->data() + file->offset, *count);
    file->offset += *count;
    return LV_FS_RES_OK;
}
inline lv_fs_res_t lv_fs_close(lv_fs_file_t* file)
{
    if (!file->open) return LV_FS_RES_UNKNOWN;
    file->open = false;
    --kml_reader_fake::open_handles;
    return kml_reader_fake::close_fail ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}
inline lv_fs_res_t lv_fs_dir_open(lv_fs_dir_t*, const char*) { return LV_FS_RES_UNKNOWN; }
inline lv_fs_res_t lv_fs_dir_close(lv_fs_dir_t*) { return LV_FS_RES_OK; }
