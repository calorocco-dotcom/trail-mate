#pragma once
#include "music/reader.h"
#include "music/service.h"
namespace music
{
enum class DecodeResult : uint8_t
{
    Frame,
    End,
    Invalid,
    IoError
};
struct PcmFrame
{
    int16_t* samples = nullptr;
    uint16_t count = 0;
    uint32_t rate = 0;
};
class Decoder
{
    struct Workspace;
    Workspace* state_ = nullptr;

  public:
    Decoder();
    ~Decoder();
    bool ready() const { return state_ != nullptr; }
    bool open(Reader& input, Track& metadata);
    DecodeResult next(PcmFrame& out);
    bool seek(uint32_t milliseconds);
    uint32_t position_ms() const;
    static std::size_t workspace_bytes();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
};
} // namespace music
