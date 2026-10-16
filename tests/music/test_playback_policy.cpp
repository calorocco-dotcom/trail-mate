#include "music/playback_policy.h"
#include <cassert>
int main()
{
    music::PlaybackIntent intent;
    intent.play();
    assert(intent.may_open_audio(false) && !intent.may_open_audio(true));
    assert(intent.state(true) == music::Playback::Interrupted);
    intent.pause();
    assert(!intent.may_open_audio(false));
    music::QueueCursor queue;
    queue.reset(3, 2, 17);
    assert(queue.next(music::PlayMode::RepeatAll, true) == 0);
    assert(queue.next(music::PlayMode::RepeatOne, true) == 0);
    assert(queue.next(music::PlayMode::RepeatOne, false) == 1);
    assert(queue.previous(music::PlayMode::RepeatAll) == 0);
    for (unsigned n = 0; n < 100; n++)
    {
        const auto old = queue.current();
        const auto next = queue.next(music::PlayMode::Shuffle, true);
        assert(next != old && next < 3);
        assert(queue.previous(music::PlayMode::Shuffle) == old);
    }
    queue.reset(1, 0, 1);
    assert(queue.next(music::PlayMode::Shuffle, true) == 0);
    queue.reset(0, 0, 1);
    assert(queue.next(music::PlayMode::RepeatAll, true) == 0 && queue.previous(music::PlayMode::Shuffle) == 0);
}
