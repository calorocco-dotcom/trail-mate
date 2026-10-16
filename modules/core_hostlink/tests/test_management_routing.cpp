#include "hostlink/hostlink_frame_router.h"
#include "hostlink/hostlink_service_codec.h"
#include <cassert>

// sequence occupies pre-existing padding. The existing queue's per-item RAM
// budget must not grow when management reuses the legacy command queue.
static_assert(sizeof(hostlink::PendingCommand) == 528, "HostLink command queue RAM budget changed");

int main()
{
    hostlink::Frame frame;
    frame.type = static_cast<uint8_t>(hostlink::FrameType::ManagementRequest);
    assert(hostlink::route_frame(frame, false).type == hostlink::HostlinkFrameDecisionType::RejectNotReady);
    const auto ready = hostlink::route_frame(frame, true);
    assert(ready.type == hostlink::HostlinkFrameDecisionType::DispatchCommand);
    assert(ready.command == hostlink::HostlinkCommandId::Management);
    frame.type = static_cast<uint8_t>(hostlink::FrameType::CmdGetConfig);
    assert(hostlink::route_frame(frame, true).command == hostlink::HostlinkCommandId::GetConfig);
    frame.type = 0x7f;
    assert(hostlink::route_frame(frame, true).type == hostlink::HostlinkFrameDecisionType::Unsupported);
}
