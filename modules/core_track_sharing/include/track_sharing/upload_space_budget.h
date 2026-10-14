#pragma once
#include "track_sharing/upload_paths.h"
#include <cstdint>
namespace track_sharing
{
class UploadSpaceBudget
{
  public:
    explicit UploadSpaceBudget(uint32_t bytes, uint32_t maximum = 8388608) : valid_((maximum == 8388608 || maximum == 4096 || maximum == 4166 || maximum == 1100 || maximum == 294912) && bytes && bytes <= maximum)
    {
        if (valid_) remaining_ = 2ULL * (maximum + 216) - bytes - 216;
    }
    UploadSpaceBudget(uint32_t bytes, UploadKind kind) : UploadSpaceBudget(bytes, uploadByteLimit(kind))
    {
        if (valid_ && kind == UploadKind::AttachmentResource)
        {
            // One admitted 64-resource package; every unknown/orphan file and
            // journal counts. Does not enlarge the original two-KML quota.
            remaining_ = 64ULL * 1024 * 1024 + 64 * 216 - bytes - 216;
            file_limit_ = 64 * 3;
        }
    }
    bool add(uint64_t bytes, bool directory)
    {
        if (!valid_ || directory || files_ >= file_limit_ - 3 || bytes > remaining_) return valid_ = false;
        ++files_;
        remaining_ -= bytes;
        return true;
    }
    bool complete() const { return valid_; }
    static constexpr uint64_t kBytes = 2ULL * (8388608 + 216);
    static constexpr unsigned kFiles = 6;

  private:
    uint64_t remaining_ = 0;
    uint16_t files_ = 0;
    uint16_t file_limit_ = kFiles;
    bool valid_;
};
static_assert(sizeof(UploadSpaceBudget) <= 16, "Upload quota cannot hold a directory list");
} // namespace track_sharing
