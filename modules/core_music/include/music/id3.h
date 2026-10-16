#pragma once
#include "music/reader.h"
#include "music/service.h"
#include <algorithm>
namespace music
{
// Allocate the parser with music::make: its bounded text workspace belongs in PSRAM.
// Scans frame headers and requested fields only; APIC/USLT payloads remain in the file.
class TagReader
{
  public:
    bool read(Reader& input, Track& track, MetadataPage* page = nullptr, uint16_t offset = 0);
    bool read_text(Reader& input, uint32_t start, uint32_t end, uint8_t encoding, char* out, std::size_t capacity)
    {
        input_ = &input;
        return text(start, std::min(end, input.size()), encoding, out, capacity);
    }

  private:
    Reader* input_ = nullptr;
    MetadataPage* page_ = nullptr;
    uint8_t buffer_[512]{};
    uint8_t byte_cache_[128]{};
    uint32_t byte_offset_ = 0;
    uint16_t byte_count_ = 0;
    char value_[384]{};
    uint16_t fields_ = 0;
    bool exact(uint32_t offset, void* output, std::size_t size);
    bool byte(uint32_t offset, uint8_t& value);
    uint32_t terminated(uint32_t start, uint32_t end, uint8_t encoding);
    bool text(uint32_t start, uint32_t end, uint8_t encoding, char* out, std::size_t capacity, bool unsync = false);
    void field(const char* id, const char* name, const char* value, bool truncated = false);
    void parse_picture(uint32_t start, uint32_t end, bool legacy, Track& track);
    void parse_frame(const char* id, uint32_t start, uint32_t end, bool unsync, Track& track);
};
} // namespace music
