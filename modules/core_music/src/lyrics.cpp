#include "music/lyrics.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace music
{
void Lyrics::clear()
{
    reader_ = nullptr;
    count_ = 0;
    current_ = -2;
    manual_ = 0;
    cache_at_ = UINT32_MAX;
    cache_size_ = 0;
    offset_ms_ = 0;
    synchronized_ = truncated_ = false;
}
bool Lyrics::byte(uint32_t at, uint8_t& value)
{
    if (cache_at_ == UINT32_MAX || at < cache_at_ || at >= cache_at_ + cache_size_)
    {
        cache_at_ = at;
        cache_size_ = reader_->read_at(at, cache_, std::min<uint32_t>(sizeof(cache_), reader_->size() - at));
    }
    if (at >= cache_at_ + cache_size_) return false;
    value = cache_[at - cache_at_];
    return true;
}
bool Lyrics::open(Reader& input, bool lrc, uint32_t offset, uint32_t bytes, uint8_t encoding)
{
    clear();
    reader_ = &input;
    encoding_ = encoding;
    if (offset > input.size()) return false;
    const uint32_t available = std::min(bytes, input.size() - offset), length = std::min<uint32_t>(available, 64 * 1024), end = offset + length;
    truncated_ = available > length;
    // Detect sidecar UTF8/UTF16 BOMs. The scan itself always keeps raw file offsets.
    uint8_t a = 0, b = 0, c = 0;
    if (length >= 2 && byte(offset, a) && byte(offset + 1, b))
    {
        if (a == 0xff && b == 0xfe)
        {
            encoding_ = 1;
            offset += 2;
        }
        else if (a == 0xfe && b == 0xff)
        {
            encoding_ = 2;
            offset += 2;
        }
        else if (length >= 3 && a == 0xef && b == 0xbb && byte(offset + 2, c) && c == 0xbf)
        {
            encoding_ = 3;
            offset += 3;
        }
    }
    const unsigned step = encoding_ == 1 || encoding_ == 2 ? 2 : 1;
    uint32_t start = offset;
    auto append = [&](uint32_t time, uint32_t begin, uint32_t finish)
    {if(count_==512){truncated_=true;return;}lines_[count_++]={time,begin,finish}; };
    for (uint32_t at = offset; at <= end; at += step)
    {
        bool newline = at == end;
        if (!newline)
        {
            if (!byte(at, a)) return false;
            if (step == 2)
            {
                if (at + 1 >= end) break;
                if (!byte(at + 1, b)) return false;
            }
            const unsigned cp = step == 1 ? a : encoding_ == 1 ? unsigned(a) | (unsigned(b) << 8)
                                                               : (unsigned(a) << 8) | b;
            newline = cp == 10;
        }
        if (!newline) continue;
        if (at > start)
        {
            decoder_.read_text(input, start, at, encoding_, text_, sizeof(text_));
            if (lrc)
            {
                if (!std::strncmp(text_, "[offset:", 8))
                {
                    const auto value = std::strtol(text_ + 8, nullptr, 10);
                    offset_ms_ = std::clamp<long>(value, -3600000, 3600000);
                }
                else
                {
                    uint32_t stamps[8]{};
                    unsigned n = 0;
                    const char* cursor = text_;
                    while (*cursor == '[' && n < 8)
                    {
                        unsigned minute = 0, second = 0, millis = 0;
                        int consumed = 0;
                        if (std::sscanf(cursor, "[%u:%u%n", &minute, &second, &consumed) != 2 || minute > 999 || second > 59) break;
                        const char* tail = cursor + consumed;
                        if (*tail == '.' || *tail == ':')
                        {
                            ++tail;
                            unsigned scale = 100;
                            while (*tail >= '0' && *tail <= '9')
                            {
                                if (scale)
                                {
                                    millis += (*tail - '0') * scale;
                                    scale /= 10;
                                }
                                ++tail;
                            }
                        }
                        if (*tail != ']') break;
                        stamps[n++] = (minute * 60 + second) * 1000 + millis;
                        cursor = tail + 1;
                    }
                    if (n && *cursor)
                    {
                        // Timestamp syntax is ASCII: each consumed code point has fixed encoded width.
                        const uint32_t begin = start + static_cast<uint32_t>(cursor - text_) * step;
                        for (unsigned i = 0; i < n; i++) append(stamps[i], begin, at);
                    }
                }
            }
            else if (text_[0]) append(0, start, at);
        }
        start = at + step;
        if (at == end) break;
    }
    synchronized_ = lrc && count_ != 0;
    if (synchronized_) std::sort(lines_, lines_ + count_, [](const Line& left, const Line& right)
                                 { return left.time < right.time; });
    return count_ != 0;
}
bool Lyrics::update(uint32_t position_ms, Snapshot& out)
{
    out.has_lyrics = count_ != 0;
    out.synchronized_lyrics = synchronized_;
    int selected = manual_;
    if (synchronized_)
    {
        const int64_t now = int64_t(position_ms) - offset_ms_;
        selected = -1;
        for (unsigned i = 0; i < count_ && int64_t(lines_[i].time) <= now; i++) selected = i;
    }
    if (selected == current_) return false;
    current_ = selected;
    char* labels[] = {out.lyric_previous, out.lyric_current, out.lyric_next};
    for (unsigned index = 0; index < 3; index++)
    {
        labels[index][0] = 0;
        const int line = selected + int(index) - 1;
        if (line >= 0 && line < count_) decoder_.read_text(*reader_, lines_[line].start, lines_[line].end, encoding_, labels[index], 192);
    }
    return true;
}
} // namespace music
