#include "boards/t_display_p4/codec_compat.h"

#include <algorithm>
#include <cmath>

namespace boards::t_display_p4
{

CodecCompat::CodecCompat(trail_mate_t_display_p4_audio_owner_t owner) : owner_(owner) {}

CodecCompat::~CodecCompat()
{
    close();
}

int CodecCompat::open(uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate)
{
    if (bits_per_sample != 16 || channels != 1 || sample_rate == 0)
    {
        return -1;
    }
    if (!trail_mate_t_display_p4_audio_begin(owner_, sample_rate))
    {
        return -1;
    }
    open_ = true;
    if (!trail_mate_t_display_p4_audio_set_input_gain(owner_, gain_db_))
    {
        close();
        return -1;
    }
    apply_output_level();
    return 0;
}

void CodecCompat::close()
{
    if (!open_)
    {
        return;
    }
    trail_mate_t_display_p4_audio_end(owner_);
    open_ = false;
}

int CodecCompat::write(uint8_t* buffer, size_t size)
{
    if (!open_ || !buffer || size == 0 || (size % sizeof(int16_t)) != 0)
    {
        return -1;
    }
    const auto* pcm = reinterpret_cast<const int16_t*>(buffer);
    return trail_mate_t_display_p4_audio_write_mono(owner_, pcm, size / sizeof(int16_t)) ? 0 : -1;
}

int CodecCompat::read(uint8_t* buffer, size_t size)
{
    if (!open_ || !buffer || size == 0 || (size % sizeof(int16_t)) != 0)
    {
        return -1;
    }
    auto* pcm = reinterpret_cast<int16_t*>(buffer);
    return trail_mate_t_display_p4_audio_read_mono(owner_, pcm, size / sizeof(int16_t)) ? 0 : -1;
}

void CodecCompat::setMute(bool enable)
{
    mute_ = enable;
    apply_output_level();
}

bool CodecCompat::getMute() const
{
    return mute_;
}

void CodecCompat::setOutMute(bool enable)
{
    out_mute_ = enable;
    apply_output_level();
}

bool CodecCompat::getOutMute() const
{
    return out_mute_;
}

void CodecCompat::setVolume(uint8_t level)
{
    volume_ = std::min<uint8_t>(level, 100U);
    apply_output_level();
}

int CodecCompat::getVolume() const
{
    return volume_;
}

void CodecCompat::setGain(float db_value)
{
    if (!std::isfinite(db_value)) return;
    const float gain = std::clamp(db_value, 0.0f, 42.0f);
    if (!open_ || trail_mate_t_display_p4_audio_set_input_gain(owner_, gain)) gain_db_ = gain;
}

float CodecCompat::getGain() const
{
    return gain_db_;
}

bool CodecCompat::ready() const
{
    return open_;
}

void CodecCompat::apply_output_level()
{
    if (open_)
    {
        trail_mate_t_display_p4_audio_set_volume_percent((mute_ || out_mute_) ? 0 : volume_);
    }
}

} // namespace boards::t_display_p4
