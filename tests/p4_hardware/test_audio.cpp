#include "../../boards/t_display_p4/src/codec_compat.cpp"
#include "../../platform/esp/idf_components/t_display_p4/t_display_p4_audio_runtime.cpp"
#include <cassert>
#include <iostream>
int main()
{
    // Fail each allocation/init boundary, then retry without leaking I2S0.
    for (int failure = 1; failure <= 7; ++failure)
    {
        release_audio_resources_locked();
        fake::now_us += 3000000;
        fake::step = 0;
        fake::fail_step = failure;
        assert(!trail_mate_t_display_p4_audio_is_ready());
        assert(fake::live == 0 && fake::channel_live == 0);
        const int failed_step = fake::step;
        assert(!trail_mate_t_display_p4_audio_is_ready());
        assert(fake::step == failed_step); // Backoff prevents allocation storms.
        fake::fail_step = -1;
        fake::now_us += 3000000;
        assert(trail_mate_t_display_p4_audio_is_ready());
        assert(fake::live == 4 && fake::channel_live == 2);
    }
    using boards::t_display_p4::CodecCompat;
    constexpr auto walkie = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_WALKIE;
    constexpr auto sstv = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_SSTV;
    CodecCompat codec(walkie);
    codec.setGain(12);
    assert(codec.open(16, 1, 16000) == 0 && fake::gain == 12);
    assert(!trail_mate_t_display_p4_audio_begin(sstv, 16000));
    assert(!trail_mate_t_display_p4_audio_set_input_gain(sstv, 24));
    codec.setGain(99);
    assert(fake::gain == 42 && codec.getGain() == 42);
    fake::gain_ok = false;
    codec.setGain(0);
    assert(codec.getGain() == 42);
    fake::gain_ok = true;
    std::array<int16_t, 641> pcm{};
    pcm.fill(123);
    assert(trail_mate_t_display_p4_audio_write_mono(walkie, pcm.data(), pcm.size()));
    assert(fake::written.size() == pcm.size() * 2);
    for (auto sample : fake::written) assert(sample == 123);
    assert(trail_mate_t_display_p4_audio_read_mono(walkie, pcm.data(), pcm.size()));
    for (auto sample : pcm) assert(sample == 100);
    codec.close();
    fake::gain_ok = false;
    assert(!trail_mate_t_display_p4_audio_begin(walkie, 16000));
    assert(s_audio.owner == TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NONE);
    release_audio_resources_locked();
    assert(fake::live == 0 && fake::channel_live == 0);
    std::cout << "P4 audio allocation recovery, ownership, gain and PCM passed\n";
}
