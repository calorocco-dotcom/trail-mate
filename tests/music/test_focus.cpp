#include "platform/audio/audio_focus.h"
#include <cassert>
using namespace platform::audio;
struct Client
{
    bool wanted_play = true, playing = true, ack = true;
    unsigned suspends = 0, resumes = 0;
    static bool suspend(void* p)
    {
        auto& self = *static_cast<Client*>(p);
        ++self.suspends;
        assert(music_blocked());
        if (self.ack) self.playing = false;
        return self.ack;
    }
    static void resume(void* p)
    {
        auto& self = *static_cast<Client*>(p);
        ++self.resumes;
        self.playing = self.wanted_play && !music_blocked();
    }
};
int main()
{
    Client client;
    register_music_focus_client({&client, Client::suspend, Client::resume});
    auto voice = request_focus(FocusOwner::VoiceMessage);
    assert(voice && music_blocked() && !client.playing);
    auto walkie = request_focus(FocusOwner::Walkie);
    assert(walkie);
    release_focus(voice);
    assert(music_blocked() && !client.playing && client.resumes == 0);
    release_focus(voice);
    assert(client.resumes == 0);
    release_focus(walkie);
    assert(!music_blocked() && client.playing && client.resumes == 1);
    auto newer = request_focus(FocusOwner::Call);
    assert(newer);
    release_focus(voice);
    assert(music_blocked());
    client.wanted_play = false;
    release_focus(newer);
    assert(!client.playing);
    client.ack = false;
    auto denied = request_focus(FocusOwner::Sstv);
    assert(!denied && !music_blocked());
    client.ack = true;
    FocusToken all[12];
    for (auto& token : all)
    {
        token = request_focus(FocusOwner::Notification);
        assert(token);
    }
    assert(!request_focus(FocusOwner::Notification));
    for (unsigned i = 0; i < 11; i++)
    {
        release_focus(all[i]);
        assert(music_blocked());
    }
    release_focus(all[11]);
    assert(!music_blocked() && !client.playing);
    register_music_focus_client({});
}
