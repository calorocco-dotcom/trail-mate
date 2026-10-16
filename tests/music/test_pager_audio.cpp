#include "platform/audio/audio_focus.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/music/music_ports.h"
#include <cassert>
namespace
{
bool blocked = false;
}
namespace platform::audio
{
bool music_blocked() { return blocked; }
} // namespace platform::audio
int main()
{
    namespace port = platform::esp::music_runtime;
    namespace board = platform::esp::boards;
    assert(port::audio_supported());
    board::available = false;
    assert(!port::audio_begin(44100));
    board::available = true;
    assert(port::audio_begin(44100));
    assert(board::output.opened && board::output.rate == 44100);
    int16_t pcm[256]{};
    assert(port::audio_write(pcm, 256)); // Successful codec status is 0, not 512 bytes.
    assert(board::output.data == reinterpret_cast<const uint8_t*>(pcm) && board::output.bytes == sizeof(pcm));
    board::output.write_result = 512;
    assert(!port::audio_write(pcm, 256)); // Never confuse a nonzero status with a byte count.
    board::output.write_result = -1;
    assert(!port::audio_write(pcm, 256));
    port::audio_end();
    assert(!board::output.opened && board::output.closes == 1);
    board::output.volume_ok = false;
    assert(!port::audio_begin(48000) && board::output.closes == 2);
    board::output.volume_ok = true;
    board::output.mute_ok = false;
    assert(!port::audio_begin(48000) && board::output.closes == 3);
    board::output.mute_ok = true;
    board::output.open_result = -1;
    assert(!port::audio_begin(48000) && board::output.closes == 3);
    const auto opens = board::output.opens;
    blocked = true;
    assert(!port::audio_begin(48000) && board::output.opens == opens);
}
