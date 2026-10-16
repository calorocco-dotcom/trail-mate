#include "platform/audio/audio_focus.h"
#include "platform/esp/common/music/music_ports.h"
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "bsp/trail_mate_t_display_p4_runtime.h"
#elif defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
#include "platform/esp/boards/board_runtime.h"
#if defined(ARDUINO_T_LORA_PAGER)
#include "boards/tlora_pager/tlora_pager_board.h"
#elif defined(ARDUINO_T_DECK)
#include "boards/tdeck/tdeck_board.h"
#else
#include "platform/esp/wio_tracker_l2/wio_tracker_l2_board.h"
#endif
#endif
namespace platform::esp::music_runtime
{
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
namespace
{
#if defined(ARDUINO_T_LORA_PAGER)
using MusicBoard = ::boards::tlora_pager::TLoRaPagerBoard;
constexpr auto kOwner = ::boards::tlora_pager::PagerAudioOwner::Music;
#elif defined(ARDUINO_T_DECK)
using MusicBoard = ::boards::tdeck::TDeckBoard;
#else
using MusicBoard = ::boards::wio_tracker_l2::WioTrackerL2Board;
#endif
MusicBoard* board()
{
    ::platform::esp::boards::AppContextInitHandles handles;
    return ::platform::esp::boards::tryResolveAppContextInitHandles(&handles) && handles.board ? static_cast<MusicBoard*>(handles.board) : nullptr;
}
} // namespace
#endif
bool audio_supported()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
    return true;
#else
    return false;
#endif
}
bool audio_begin(uint32_t rate)
{
    if (::platform::audio::music_blocked()) return false;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return trail_mate_t_display_p4_audio_begin(TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC, rate);
#elif defined(ARDUINO_T_LORA_PAGER)
    auto* output = board();
    if (!output || output->openAudioSession(kOwner, 16, 1, rate, true) != 0) return false;
    if (!output->audioSetVolume(kOwner, 100) || !output->audioSetOutMute(kOwner, false))
    {
        output->closeAudioSession(kOwner);
        return false;
    }
    return true;
#elif defined(ARDUINO_T_DECK)
    auto* output = board();
    return output && output->audioRuntime().beginMusic(rate);
#elif defined(ARDUINO_WIO_TRACKER_L2)
    auto* output = board();
    return output && output->beginMusic(rate);
#else
    (void)rate;
    return false;
#endif
}
bool audio_write(const int16_t* samples, std::size_t count)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return trail_mate_t_display_p4_audio_write_mono(TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC, samples, count);
#elif defined(ARDUINO_T_LORA_PAGER)
    auto* output = board();
    return output && output->audioWrite(kOwner, reinterpret_cast<const uint8_t*>(samples), count * sizeof(int16_t)) == static_cast<int>(count * sizeof(int16_t));
#elif defined(ARDUINO_T_DECK)
    auto* output = board();
    return output && output->audioRuntime().writeMusic(samples, count);
#elif defined(ARDUINO_WIO_TRACKER_L2)
    auto* output = board();
    return output && output->writeMusic(samples, count);
#else
    (void)samples;
    (void)count;
    return false;
#endif
}
void audio_end()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    trail_mate_t_display_p4_audio_end(TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_MUSIC);
#elif defined(ARDUINO_T_LORA_PAGER)
    if (auto* output = board()) output->closeAudioSession(kOwner);
#elif defined(ARDUINO_T_DECK)
    if (auto* output = board()) output->audioRuntime().endMusic();
#elif defined(ARDUINO_WIO_TRACKER_L2)
    if (auto* output = board()) output->endMusic();
#endif
}
} // namespace platform::esp::music_runtime
