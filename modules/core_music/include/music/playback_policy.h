#pragma once
#include "music/service.h"
namespace music
{
// Separate user intent from a transient loss of hardware focus.
struct PlaybackIntent
{
    bool wanted = false;
    void play() { wanted = true; }
    void pause() { wanted = false; }
    void stop() { wanted = false; }
    Playback state(bool blocked) const { return wanted ? (blocked ? Playback::Interrupted : Playback::Playing) : Playback::Paused; }
    bool may_open_audio(bool blocked) const { return wanted && !blocked; }
};
class QueueCursor
{
    uint32_t current_ = 0, count_ = 0, random_ = 0x6419d2f3;
    uint32_t history_[32]{};
    uint8_t history_size_ = 0;

  public:
    void reset(uint32_t count, uint32_t index, uint32_t seed)
    {
        count_ = count;
        current_ = count ? index % count : 0;
        random_ = seed ? seed : 1;
        history_size_ = 0;
    }
    uint32_t current() const { return current_; }
    uint32_t next(PlayMode mode, bool natural_end)
    {
        if (count_ < 2 || (natural_end && mode == PlayMode::RepeatOne)) return current_;
        if (history_size_ == 32)
        {
            for (unsigned i = 1; i < 32; i++) history_[i - 1] = history_[i];
            --history_size_;
        }
        history_[history_size_++] = current_;
        if (mode == PlayMode::Shuffle)
        {
            random_ ^= random_ << 13;
            random_ ^= random_ >> 17;
            random_ ^= random_ << 5;
            const auto pick = random_ % (count_ - 1);
            current_ = pick >= current_ ? pick + 1 : pick;
        }
        else current_ = (current_ + 1) % count_;
        return current_;
    }
    uint32_t previous(PlayMode mode)
    {
        if (!count_) return 0;
        if (mode == PlayMode::Shuffle && history_size_) current_ = history_[--history_size_];
        else current_ = current_ ? current_ - 1 : count_ - 1;
        return current_;
    }
};
} // namespace music
