#include "platform/esp/idf_common/hostlink_track_directories_runtime.h"
#include <cassert>
#include <vector>

struct Adapter : chat::IMeshAdapter
{
    const chat::lxmf::TrackDeliveryBinding* binding = nullptr;
    unsigned binds = 0;
    bool allow_bind = true;
    bool binding_busy = false;
    bool allow_send = true;
    unsigned sends = 0;
    std::vector<uint8_t> network_wire;
    unsigned path_requests = 0;
    uint8_t path_target[16]{};
    bool allow_path = true;
    bool requestTrackDirectoryPath(const uint8_t* destination) override
    {
        ++path_requests;
        std::memcpy(path_target, destination, 16);
        return allow_path;
    }
    chat::DeviceAuthorResult readDeviceAuthorKey(uint8_t* key, uint32_t& generation) const override
    {
        std::memset(key, 7, 64);
        generation = 9;
        return chat::DeviceAuthorResult::Ok;
    }
    bool trackLocalDestination(uint8_t* value) override
    {
        std::memset(value, 8, 16);
        return true;
    }
    chat::MeshSendResult sendTrackData(const uint8_t* destination, chat::lxmf::ByteSpan body, const uint8_t* author, uint32_t generation, bool response, std::array<uint8_t, 32>*) override
    {
        assert(destination[0] == 4 && author[0] == 7 && generation == 9 && !response);
        ++sends;
        network_wire.assign(body.data, body.data + body.size);
        return allow_send ? chat::MeshSendResult::success(1) : chat::MeshSendResult::fail(chat::MeshOperationFailure::Busy);
    }
    bool bindTrackDelivery(const chat::lxmf::TrackDeliveryBinding* value) override
    {
        ++binds;
        if (!allow_bind) return false;
        binding = value;
        return true;
    }
    chat::TrackBindingChange replaceTrackDelivery(const chat::lxmf::TrackDeliveryBinding* expected, const chat::lxmf::TrackDeliveryBinding* replacement) override
    {
        ++binds;
        if (!allow_bind) return chat::TrackBindingChange::Unsupported;
        if (binding_busy) return chat::TrackBindingChange::Busy;
        if (binding != expected) return chat::TrackBindingChange::Conflict;
        binding = replacement;
        return chat::TrackBindingChange::Applied;
    }
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override { return false; }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId, uint32_t, const uint8_t*, size_t, chat::NodeId, bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }
};
int main()
{
    using namespace hostlink::management;
    namespace runtime = directory_runtime;
    Adapter adapter;
    uint8_t session[16], other[16], body[] = {0x91, 0}, output[320];
    std::memset(session, 1, 16);
    std::memset(other, 2, 16);
    EnvelopeView request{session, other, uint16_t(Operation::TrackDirectoryList), body, sizeof(body)};
    size_t written = 0;
    directory_free_bytes = 1;
    assert(runtime::list(adapter, request, 1000, output, sizeof(output), written) == Status::Busy && !runtime::owner && !directory_live_allocations);
    directory_free_bytes = 65536;
    adapter.allow_bind = false;
    assert(runtime::list(adapter, request, 1000, output, sizeof(output), written) == Status::Unsupported && !runtime::owner && !directory_live_allocations);
    adapter.allow_bind = true;
    assert(runtime::list(adapter, request, 1000, output, sizeof(output), written) == Status::Ok && runtime::owner && directory_live_allocations == 1 && adapter.binding);
    assert(written == 3 && output[0] == 0x92 && output[1] == 0xc0 && output[2] == 0x90);
    {
        uint8_t discover_body[32], target[16];
        std::memset(target, 3, 16);
        geocaching::protocol::CmpWriter discovery_request(discover_body, sizeof(discover_body));
        assert(discovery_request.array(1) && discovery_request.binary({target, 16}));
        EnvelopeView discovery{session, other, uint16_t(Operation::TrackDirectoryDiscover), discover_body, discovery_request.size()};
        assert(runtime::discover(adapter, discovery, 1000, output, sizeof(output), written) == Status::Ok && written == 1 && output[0] == 0x90);
        assert(adapter.path_requests == 1 && !std::memcmp(adapter.path_target, target, 16) && runtime::owner->pool.size() == 0);
        adapter.allow_path = false;
        assert(runtime::discover(adapter, discovery, 1000, output, sizeof(output), written) == Status::Busy && written == 0);
        adapter.allow_path = true;
        discovery.session = other;
        assert(runtime::discover(adapter, discovery, 1000, output, sizeof(output), written) == Status::Conflict && adapter.path_requests == 2);
        discovery.session = session;
        std::memset(discover_body + 3, 0, 16);
        assert(runtime::discover(adapter, discovery, 1000, output, sizeof(output), written) == Status::InvalidField && adapter.path_requests == 2);
    }
    uint8_t discovery[16], delivery[16], public_key[64], epoch[16], metadata[128];
    std::memset(discovery, 3, 16);
    std::memset(delivery, 4, 16);
    std::memset(public_key, 5, 64);
    std::memset(epoch, 6, 16);
    geocaching::protocol::CmpWriter announcement(metadata, sizeof(metadata));
    assert(announcement.array(5) && announcement.unsignedInteger(1) && announcement.text(track_sharing::kApplicationType) && announcement.binary({delivery, 16}) && announcement.binary({epoch, 16}) && announcement.text("Node"));
    chat::lxmf::TrackDirectoryAnnouncementView view{{discovery, 16}, {delivery, 16}, {public_key, 64}, {metadata, announcement.size()}};
    assert(chat::lxmf::deliverVerifiedTrackDirectory(adapter.binding, view));
    assert(runtime::list(adapter, request, 1001, output, sizeof(output), written) == Status::Ok);
    geocaching::protocol::CmpReader reader({output, written});
    size_t fields = 0;
    uint64_t ready = 0;
    geocaching::ByteView field;
    std::string_view name;
    assert(reader.array(fields, 2) && fields == 2 && reader.nil() && reader.array(fields, 1) && fields == 1 && reader.array(fields, 8) && fields == 8);
    assert(reader.binary(field, 16) && field.size == 16 && !std::memcmp(field.data, delivery, 16));
    assert(reader.binary(field, 64) && field.size == 64 && !std::memcmp(field.data, public_key, 64));
    assert(reader.binary(field, 16) && field.size == 16 && !std::memcmp(field.data, epoch, 16));
    assert(reader.text(name, 40) && name == "Node" && reader.unsignedInteger(ready) && ready == 0);
    assert(reader.unsignedInteger(ready) && ready == 0 && reader.unsignedInteger(ready) && ready == 0 && reader.unsignedInteger(ready) && ready == 0 && reader.finished());
    runtime::PinnedDirectory pinned;
    pinned.file_limit = 123;
    assert(runtime::pin(adapter, session, {delivery, 16}, {epoch, 16}, 1002, pinned) == Status::Unauthorized && pinned.file_limit == 123);
    uint8_t network_scratch[512], reply[512], local[16], message_hash[32]{}, wrong_source[16]{};
    std::memset(local, 8, 16);
    adapter.allow_send = false;
    runtime::advance(1002, true, network_scratch, sizeof(network_scratch));
    assert(adapter.sends == 1 && !runtime::owner->pending);
    adapter.allow_send = true;
    runtime::advance(1003, true, network_scratch, sizeof(network_scratch));
    assert(adapter.sends == 1);
    runtime::advance(4003, true, network_scratch, sizeof(network_scratch));
    assert(adapter.sends == 2 && runtime::owner->pending);
    runtime::advance(4004, true, network_scratch, sizeof(network_scratch));
    assert(adapter.sends == 2);
    geocaching::protocol::CmpReader outgoing({adapter.network_wire.data(), adapter.network_wire.size()});
    uint64_t value = 0;
    geocaching::ByteView request_id;
    assert(outgoing.array(fields, 6) && fields == 6 && outgoing.unsignedInteger(value) && value == 1 && outgoing.unsignedInteger(value) && value == 0 &&
           outgoing.unsignedInteger(value) && value == 0 && outgoing.binary(request_id, 16) && request_id.size == 16 && outgoing.unsignedInteger(value) && value == 8192 &&
           outgoing.array(fields, 0) && fields == 0 && outgoing.finished());
    geocaching::protocol::CmpWriter capabilities(reply, sizeof(reply));
    assert(capabilities.array(6) && capabilities.unsignedInteger(1) && capabilities.unsignedInteger(1) && capabilities.unsignedInteger(0) &&
           capabilities.binary(request_id) && capabilities.unsignedInteger(200) && capabilities.array(8) && capabilities.unsignedInteger(1) &&
           capabilities.text(track_sharing::kApplicationType) && capabilities.binary({public_key, 64}) && capabilities.binary({epoch, 16}) &&
           capabilities.unsignedInteger(8388608) && capabilities.unsignedInteger(1024) && capabilities.unsignedInteger(32) && capabilities.array(9));
    for (unsigned op = 0; op < 9; ++op) assert(capabilities.unsignedInteger(op));
    chat::lxmf::CustomDeliveryView response{{wrong_source, 16}, {local, 16}, {message_hash, 32}, {reply, capabilities.size()}};
    assert(!chat::lxmf::deliverVerifiedTrack(adapter.binding, false, false, response) && runtime::owner->pending);
    response.source = {delivery, 16};
    assert(!chat::lxmf::deliverVerifiedTrack(adapter.binding, true, false, response));
    assert(chat::lxmf::deliverVerifiedTrack(adapter.binding, false, false, response) && !runtime::owner->pending && runtime::owner->pool.at(0)->capabilities_verified);
    runtime::advance(4005, true, network_scratch, sizeof(network_scratch));
    assert(adapter.sends == 2);
    assert(runtime::list(adapter, request, 4005, output, sizeof(output), written) == Status::Ok);
    geocaching::protocol::CmpReader negotiated({output, written});
    assert(negotiated.array(fields, 2) && fields == 2 && negotiated.nil() && negotiated.array(fields, 1) && fields == 1 && negotiated.array(fields, 8) && fields == 8);
    for (const auto length : {size_t(16), size_t(64), size_t(16)}) assert(negotiated.binary(field, length) && field.size == length);
    assert(negotiated.text(name, 40) && name == "Node" && negotiated.unsignedInteger(value) && value == 1 &&
           negotiated.unsignedInteger(value) && value == 8388608 && negotiated.unsignedInteger(value) && value == 1024 && negotiated.unsignedInteger(value) && value == 32 && negotiated.finished());
    assert(runtime::pin(adapter, session, {delivery, 16}, {epoch, 16}, 4006, pinned) == Status::Ok);
    assert(!std::memcmp(pinned.delivery, delivery, 16) && !std::memcmp(pinned.public_key, public_key, 64) && !std::memcmp(pinned.epoch, epoch, 16));
    assert(pinned.file_limit == 8388608 && pinned.chunk_limit == 1024 && pinned.query_limit == 32);
    const auto pinned_before_failure = pinned;
    assert(runtime::pin(adapter, other, {delivery, 16}, {epoch, 16}, 4007, pinned) == Status::Unauthorized);
    assert(!std::memcmp(pinned.delivery, pinned_before_failure.delivery, 16) && !std::memcmp(pinned.public_key, pinned_before_failure.public_key, 64) &&
           !std::memcmp(pinned.epoch, pinned_before_failure.epoch, 16) && pinned.file_limit == pinned_before_failure.file_limit &&
           pinned.chunk_limit == pinned_before_failure.chunk_limit && pinned.query_limit == pinned_before_failure.query_limit);
    assert(runtime::pin(adapter, session, {pinned.delivery, 16}, {epoch, 16}, 4007, pinned) == Status::InvalidField);
    runtime::owner->locked.test_and_set();
    assert(runtime::pin(adapter, session, {delivery, 16}, {epoch, 16}, 4007, pinned) == Status::Busy);
    runtime::owner->locked.clear();
    assert(runtime::pin(adapter, session, {delivery, 16}, {epoch, 16}, 34006, pinned) == Status::Expired);
    epoch[0] ^= 1;
    assert(runtime::pin(adapter, session, {delivery, 16}, {epoch, 16}, 4007, pinned) == Status::Expired);
    directory_clock_us = 5000000;
    geocaching::protocol::CmpWriter refreshed(metadata, sizeof(metadata));
    assert(refreshed.array(5) && refreshed.unsignedInteger(1) && refreshed.text(track_sharing::kApplicationType) && refreshed.binary({delivery, 16}) && refreshed.binary({epoch, 16}) && refreshed.text("Node"));
    view.app_data.size = refreshed.size();
    assert(chat::lxmf::deliverVerifiedTrackDirectory(adapter.binding, view) && !runtime::owner->pool.at(0)->capabilities_verified);
    runtime::advance(5000, true, network_scratch, sizeof(network_scratch));
    assert(adapter.sends == 3 && runtime::owner->pending);
    assert(!chat::lxmf::deliverVerifiedTrack(adapter.binding, false, false, response) && runtime::owner->pending);
    geocaching::protocol::CmpReader fresh({adapter.network_wire.data(), adapter.network_wire.size()});
    geocaching::ByteView fresh_id;
    assert(fresh.array(fields, 6) && fields == 6 && fresh.unsignedInteger(value) && value == 1 && fresh.unsignedInteger(value) && value == 0 && fresh.unsignedInteger(value) && value == 0 && fresh.binary(fresh_id, 16));
    geocaching::protocol::CmpWriter late(reply, sizeof(reply));
    assert(late.array(6) && late.unsignedInteger(1) && late.unsignedInteger(1) && late.unsignedInteger(0) && late.binary(fresh_id) && late.unsignedInteger(200) &&
           late.array(8) && late.unsignedInteger(1) && late.text(track_sharing::kApplicationType) && late.binary({public_key, 64}) && late.binary({epoch, 16}) &&
           late.unsignedInteger(8388608) && late.unsignedInteger(1024) && late.unsignedInteger(32) && late.array(9));
    for (unsigned op = 0; op < 9; ++op) assert(late.unsignedInteger(op));
    response.data.size = late.size();
    track_sharing::TrackDirectoryCapabilities late_capabilities;
    assert(track_sharing::decodeTrackCapabilities({reply, late.size()}, fresh_id, late_capabilities));
    directory_clock_us = 20001000;
    assert(!chat::lxmf::deliverVerifiedTrack(adapter.binding, false, false, response) && runtime::owner->pending);
    runtime::advance(20001, true, network_scratch, sizeof(network_scratch));
    assert(!runtime::owner->pending && adapter.sends == 3);
    assert(runtime::list(adapter, request, 20002, output, sizeof(output), written) == Status::Ok);
    runtime::advance(20003, true, network_scratch, sizeof(network_scratch));
    assert(adapter.sends == 3);
    request.session = other;
    assert(runtime::list(adapter, request, 20003, output, sizeof(output), written) == Status::Conflict);
    request.session = session;
    runtime::owner->locked.test_and_set();
    assert(runtime::list(adapter, request, 20003, output, sizeof(output), written) == Status::Busy);
    assert(!chat::lxmf::deliverVerifiedTrackDirectory(adapter.binding, view));
    runtime::owner->locked.clear();
    runtime::closeSession(other);
    assert(runtime::owner);
    runtime::advance(50002, true);
    assert(!runtime::owner && !adapter.binding && !directory_live_allocations);
    assert(runtime::list(adapter, request, 70000, output, sizeof(output), written) == Status::Ok);
    runtime::advance(70001, false);
    assert(!runtime::owner && !adapter.binding && !directory_live_allocations);
    assert(runtime::list(adapter, request, 73000, output, sizeof(output), written) == Status::Ok);
    runtime::closeSession(session);
    assert(!runtime::owner && !adapter.binding && !directory_live_allocations);
    assert(!std::memcmp(pinned.public_key, public_key, 64) && !std::memcmp(pinned.epoch, pinned_before_failure.epoch, 16));
    uint8_t persisted[track_sharing::kPublicationDirectoryBytes];
    track_sharing::PublicationDirectory recovered;
    assert(track_sharing::encodePublicationDirectory(pinned, persisted, sizeof(persisted)));
    assert(track_sharing::decodePublicationDirectory(persisted, sizeof(persisted), recovered));
    assert(track_sharing::publicationDirectoriesEqual(pinned, recovered));
    chat::lxmf::TrackDeliveryBinding job;
    job.handler=[](const chat::lxmf::CustomDeliveryView&,void*) {return false;};
    job.expected_local_author=public_key;job.expected_author_generation=1;
    assert(runtime::list(adapter,request,80000,output,sizeof(output),written)==Status::Ok && runtime::owner && directory_live_allocations==1);
    const auto* directory_binding=adapter.binding;
    adapter.binding_busy=true;
    assert(runtime::handover(adapter,&job)==chat::TrackBindingChange::Busy && adapter.binding==directory_binding && runtime::owner && directory_live_allocations==1);
    assert(!runtime::release() && runtime::owner && adapter.binding==directory_binding);
    adapter.binding_busy=false;
    assert(runtime::handover(adapter,&job)==chat::TrackBindingChange::Applied && !runtime::owner && adapter.binding==&job && !directory_live_allocations);
    runtime::closeSession(session);
    assert(adapter.binding==&job);
    assert(runtime::list(adapter,request,80001,output,sizeof(output),written)==Status::Busy && !runtime::owner && adapter.binding==&job && !directory_live_allocations);
    assert(adapter.replaceTrackDelivery(&job,nullptr)==chat::TrackBindingChange::Applied);
    assert(runtime::list(adapter,request,80002,output,sizeof(output),written)==Status::Ok && runtime::owner);
    // Another owner has replaced the slot. Retire the obsolete directory
    // context after observing Conflict, without clearing the new registration.
    adapter.binding=&job;
    assert(runtime::release() && !runtime::owner && adapter.binding==&job && !directory_live_allocations);
    assert(adapter.replaceTrackDelivery(&job,nullptr)==chat::TrackBindingChange::Applied);
}
