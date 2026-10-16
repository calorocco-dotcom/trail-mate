#pragma once
#include "music/id3.h"
namespace music
{
// Caller-owned PSRAM. Retains offsets/timestamps and only three visible text lines.
class Lyrics
{
    struct Line
    {
        uint32_t time = 0, start = 0, end = 0;
    };
    Line lines_[512]{};
    Reader* reader_ = nullptr;
    TagReader decoder_;
    uint8_t cache_[512]{};
    char text_[512]{};
    uint32_t cache_at_ = UINT32_MAX, cache_size_ = 0;
    uint16_t count_ = 0;
    int current_ = -2, manual_ = 0;
    int32_t offset_ms_ = 0;
    uint8_t encoding_ = 3;
    bool synchronized_ = false, truncated_ = false;
    bool byte(uint32_t at, uint8_t& value);

  public:
    bool open(Reader& input, bool lrc, uint32_t offset = 0, uint32_t bytes = UINT32_MAX, uint8_t encoding = 3);
    void clear();
    void scroll(int delta)
    {
        manual_ = std::clamp(manual_ + delta, 0, std::max(0, int(count_) - 1));
        current_ = -2;
    }
    bool update(uint32_t position_ms, Snapshot& out);
    bool truncated() const { return truncated_; }
};
} // namespace music
