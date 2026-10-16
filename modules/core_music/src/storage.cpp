#include "music/storage.h"
#include <algorithm>
#include <cstring>
namespace music
{
uint64_t path_id(const char* text)
{
    uint64_t hash = 14695981039346656037ULL;
    while (*text)
    {
        uint8_t c = *text++;
        if (c >= 'A' && c <= 'Z') c += 32;
        hash = (hash ^ c) * 1099511628211ULL;
    }
    return hash ? hash : 1;
}
bool valid_music_path(const char* path)
{
    if (std::strncmp(path, "/Music/", 7)) return false;
    unsigned component = 0;
    const char* start = path + 7;
    for (const char* p = start;; ++p)
    {
        const uint8_t c = *p;
        if (c && c < 32) return false;
        if (c == '\\' || c == ':') return false;
        if (!c || c == '/')
        {
            if (!component || (component == 1 && *start == '.') || (component == 2 && start[0] == '.' && start[1] == '.')) return false;
            if (!c) return true;
            component = 0;
            start = p + 1;
        }
        else ++component;
    }
}
void copy_text(char* output, std::size_t capacity, const char* text)
{
    if (!capacity) return;
    std::size_t n = std::min(capacity - 1, std::strlen(text));
    while (n && (static_cast<uint8_t>(text[n]) & 0xc0) == 0x80) --n;
    std::memcpy(output, text, n);
    output[n] = 0;
}
} // namespace music
