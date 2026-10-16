#pragma once
#include "chat/ports/track_delivery_binding.h"

namespace chat
{
class ITrackTransport
{
  public:
    virtual ~ITrackTransport() = default;
    virtual bool requestDirectoryPath(const uint8_t*) { return false; }
    // Input is borrowed for this call; deferred LXMF work owns its encoding.
    virtual MeshSendResult sendTrackData(const uint8_t destination[16], lxmf::ByteSpan data,
                                         bool response, std::array<uint8_t, 32>* accepted_hash) = 0;
    // Lifetime is owned by the application worker; unbind before destruction.
    virtual void setTrackDeliveryBinding(const lxmf::TrackDeliveryBinding* binding) = 0;
};
} // namespace chat
