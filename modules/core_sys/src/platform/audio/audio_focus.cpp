#include "platform/audio/audio_focus.h"
#include "platform/sync/resource_lock.h"
namespace platform::audio
{
namespace
{
struct Slot
{
    uint32_t generation = 0;
    FocusOwner owner = FocusOwner::Notification;
};
Slot slots[12];
uint32_t sequence = 0;
MusicFocusClient music;
struct Lock
{
    platform::sync::Guard guard{platform::sync::Resource::AudioFocus};
};
bool blocked_locked()
{
    for (const auto& slot : slots)
        if (slot.generation) return true;
    return false;
}
} // namespace
void register_music_focus_client(MusicFocusClient client)
{
    Lock lock;
    music = client;
}
FocusToken request_focus(FocusOwner owner)
{
    FocusToken token;
    MusicFocusClient client;
    {
        Lock lock;
        for (unsigned index = 0; index < 12; index++)
            if (!slots[index].generation)
            {
                if (++sequence == 0) ++sequence;
                slots[index] = {sequence, owner};
                token = {sequence, static_cast<uint8_t>(index)};
                break;
            }
        client = music;
    }
    if (!token) return {};
    // Reservation is visible before waiting, so a simultaneous music open must yield.
    // Callbacks are always outside the critical section, including failed-acquire rollback.
    if (client.suspend && !client.suspend(client.context))
    {
        release_focus(token);
        return {};
    }
    return token;
}
void release_focus(FocusToken token)
{
    if (!token || token.slot >= 12) return;
    MusicFocusClient client;
    bool resume = false;
    {
        Lock lock;
        auto& slot = slots[token.slot];
        if (slot.generation != token.generation) return;
        slot.generation = 0;
        resume = !blocked_locked();
        client = music;
    }
    if (resume && client.resume) client.resume(client.context);
}
bool music_blocked()
{
    Lock lock;
    return blocked_locked();
}
} // namespace platform::audio
