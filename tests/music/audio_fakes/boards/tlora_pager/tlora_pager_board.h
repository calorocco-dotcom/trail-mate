#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
namespace boards::tlora_pager
{
enum class PagerAudioOwner
{
    Music
};
class TLoRaPagerBoard
{
  public:
    int open_result = 0, write_result = 0;
    bool volume_ok = true, mute_ok = true, opened = false;
    unsigned opens = 0, closes = 0, writes = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    std::size_t bytes = 0;
    int openAudioSession(PagerAudioOwner, uint8_t bits, uint8_t channels, uint32_t hz, bool speaker)
    {
        assert(bits == 16 && channels == 1 && speaker);
        ++opens;
        rate = hz;
        opened = open_result == 0;
        return open_result;
    }
    bool audioSetVolume(PagerAudioOwner, uint8_t volume)
    {
        assert(volume == 100);
        return volume_ok;
    }
    bool audioSetOutMute(PagerAudioOwner, bool mute)
    {
        assert(!mute);
        return mute_ok;
    }
    int audioWrite(PagerAudioOwner, const uint8_t* input, std::size_t size)
    {
        if (!opened || !input || !size) return -1;
        data = input;
        bytes = size;
        ++writes;
        return write_result; // EspCodec/esp_codec_dev returns a status, 0 = OK.
    }
    void closeAudioSession(PagerAudioOwner)
    {
        opened = false;
        ++closes;
    }
};
} // namespace boards::tlora_pager
