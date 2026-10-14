#include "chat/ports/track_delivery_binding.h"
#include "track_sharing/directory_announcement.h"
#include <cassert>
#include <string>

struct State
{
    unsigned calls = 0;
    bool durable = false;
};
bool accept(const chat::lxmf::CustomDeliveryView&, void* context)
{
    auto& state = *static_cast<State*>(context);
    ++state.calls;
    return state.durable;
}
bool announcement(const chat::lxmf::TrackDirectoryAnnouncementView&, void* context)
{
    ++*static_cast<unsigned*>(context);
    return true;
}
int main()
{
    using namespace chat::lxmf;
    const char type[] = "trailmate.track.v1";
    assert(isTrackCustomType({reinterpret_cast<const uint8_t*>(type), sizeof(type) - 1}));
    assert(!isTrackCustomType({reinterpret_cast<const uint8_t*>(type), sizeof(type)}));
    assert(!isTrackCustomType({nullptr, sizeof(type) - 1}));
    uint8_t address[16]{}, hash[32]{}, bytes[8193]{};
    CustomDeliveryView view{{address, 16}, {address, 16}, {hash, 32}, {bytes, 8192}};
    State state;
    TrackDeliveryBinding binding{accept, &state};
    assert(!deliverVerifiedTrack(&binding, true, false, view));
    assert(!deliverVerifiedTrack(&binding, false, true, view));
    assert(!deliverVerifiedTrack(nullptr, false, false, view));
    assert(state.calls == 0);
    assert(!deliverVerifiedTrack(&binding, false, false, view) && state.calls == 1);
    state.durable = true;
    assert(deliverVerifiedTrack(&binding, false, false, view) && state.calls == 2);
    view.data.size = 8193;
    assert(!deliverVerifiedTrack(&binding, false, false, view) && state.calls == 2);
    struct Identity
    {
        uint8_t public_key[64]{};
        uint32_t generation=7;
        bool ready=true,error=false;
        bool isReady() const { return ready; }
        bool identityStorageError() const { return error; }
        uint32_t authorGeneration() const { return generation; }
        const uint8_t* encryptionPublicKey() const { return public_key; }
        const uint8_t* signingPublicKey() const { return public_key+32; }
    } identity;
    uint8_t expected_public[64];std::memset(expected_public,4,64);std::memcpy(identity.public_key,expected_public,64);
    binding.expected_local_author=expected_public;binding.expected_author_generation=7;
    view.source.size=16;view.data.size=1;
    const auto before_guard=state.calls;
    assert(deliverVerifiedTrackForAuthor(&binding,false,false,view,identity) && state.calls==before_guard+1);
    identity.ready=false;assert(!deliverVerifiedTrackForAuthor(&binding,false,false,view,identity));identity.ready=true;
    identity.error=true;assert(!deliverVerifiedTrackForAuthor(&binding,false,false,view,identity));identity.error=false;
    ++identity.generation;assert(!deliverVerifiedTrackForAuthor(&binding,false,false,view,identity));--identity.generation;
    identity.public_key[0]^=1;assert(!deliverVerifiedTrackForAuthor(&binding,false,false,view,identity));identity.public_key[0]^=1;
    identity.public_key[32]^=1;assert(!deliverVerifiedTrackForAuthor(&binding,false,false,view,identity));identity.public_key[32]^=1;
    assert(state.calls==before_guard+1);
    binding.expected_author_generation=0;assert(!deliverVerifiedTrackForAuthor(&binding,false,false,view,identity));
    binding.expected_author_generation=7;binding.expected_local_author=nullptr;assert(!deliverVerifiedTrackForAuthor(&binding,false,false,view,identity));
    binding.expected_author_generation=0;identity.ready=false;
    assert(trackBindingAuthorMatches(&binding,identity));
    state.calls=before_guard;
    view.data.size = 1;
    view.source.size = 15;
    assert(!deliverVerifiedTrack(&binding, false, false, view) && state.calls == 2);
    unsigned announcements = 0;
    TrackDeliveryBinding directory_binding{nullptr, &announcements, announcement};
    uint8_t public_key[64]{};
    TrackDirectoryAnnouncementView directory{{address, 16}, {address, 16}, {public_key, 64}, {bytes, 96}};
    assert(deliverVerifiedTrackDirectory(&directory_binding, directory) && announcements == 1);
    directory.app_data.size = 129;
    assert(!deliverVerifiedTrackDirectory(&directory_binding, directory) && announcements == 1);
    directory.app_data.size = 96;
    directory.public_key.size = 32;
    assert(!deliverVerifiedTrackDirectory(&directory_binding, directory) && announcements == 1);
    assert(!deliverVerifiedTrackDirectory(nullptr, directory));
    uint8_t metadata[128], delivery_address[16], epoch[16];
    std::memset(delivery_address, 3, 16);
    std::memset(epoch, 4, 16);
    geocaching::protocol::CmpWriter writer(metadata, sizeof(metadata));
    assert(writer.array(5) && writer.unsignedInteger(1) && writer.text("trailmate.track.v1") && writer.binary({delivery_address, 16}) &&
           writer.binary({epoch, 16}) && writer.text(std::string(40, 'x')));
    assert(writer.size() == 99);
    directory = {{address, 16}, {delivery_address, 16}, {public_key, 64}, {metadata, writer.size()}};
    track_sharing::DirectoryAdvertisement advertisement;
    assert(track_sharing::decodeTrackDirectoryAnnouncement(directory, advertisement) && advertisement.name.size() == 40);
    assert(deliverVerifiedTrackDirectory(&directory_binding, directory) && announcements == 2);
    delivery_address[0] ^= 1;
    assert(!track_sharing::decodeTrackDirectoryAnnouncement(directory, advertisement));
    delivery_address[0] ^= 1;
    for (size_t length = 0; length < writer.size(); ++length)
    {
        directory.app_data.size = length;
        assert(!track_sharing::decodeTrackDirectoryAnnouncement(directory, advertisement));
    }
}
