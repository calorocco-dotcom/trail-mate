#include "chat/runtime/meshtastic_position_core.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace app
{
struct Settings
{
    bool primary_channel_has_module_settings = true;
    bool secondary_channel_has_module_settings = true;
    uint32_t primary_channel_position_precision = 13;
    uint32_t secondary_channel_position_precision = 19;
};
struct Facade
{
    Settings settings{};
    const Settings& readConfig() const { return settings; }
};
Facade& configFacade()
{
    static Facade value;
    return value;
}
} // namespace app

namespace
{
namespace runtime = chat::runtime;
enum class ChannelId
{
    PRIMARY,
    SECONDARY
};
meshtastic_Data mirrored = meshtastic_Data_init_zero;
void queueMqttProxyPublishFromWire(const uint8_t*, size_t, const meshtastic_Data* data, ChannelId)
{
    if (data) mirrored = *data;
}
bool appConsumer(meshtastic_Data& data, ChannelId out_channel)
{
    struct
    {
        meshtastic_Data& decoded;
    } scratch{data};
    std::array<uint8_t, 256> data_buffer{}, wire_buffer{};
    size_t data_size = 0, wire_size = 0, len = data.payload.size;
    const uint8_t* payload = data.payload.bytes;
    const auto portnum = data.portnum;
    const uint32_t dest_node = 0xffffffff, node_id_ = 42;
    const bool use_pki = false;
    meshtastic_Routing_Error last_send_error_ = meshtastic_Routing_Error_NONE;
    (void)last_send_error_;
    // These fragments are extracted from the current production adapter. Only
    // the surrounding platform/config/network boundary is a native fixture.
#include "position_app_actual.inc"
    assert(payload == data.payload.bytes && len == data.payload.size);
    if (portnum == meshtastic_PortNum_POSITION_APP)
    {
        meshtastic_Data wire = meshtastic_Data_init_zero;
        pb_istream_t input = pb_istream_from_buffer(data_buffer.data(), data_size);
        assert(pb_decode(&input, meshtastic_Data_fields, &wire));
        assert(wire.payload.size == data.payload.size);
        assert(std::memcmp(wire.payload.bytes, data.payload.bytes, data.payload.size) == 0);
    }
#include "position_mirror_actual.inc"
    assert(mirrored.payload.size == data.payload.size);
    assert(std::memcmp(mirrored.payload.bytes, payload, len) == 0);
    assert(mirrored.source == 42 && mirrored.dest == 0xffffffff);
    return true;
}
bool packetConsumer(meshtastic_Data& data, uint8_t channel)
{
    struct
    {
        uint8_t channel;
    } packet{channel};
    meshtastic_Routing_Error last_send_error_ = meshtastic_Routing_Error_NONE;
    (void)last_send_error_;
#include "position_packet_actual.inc"
    return true;
}
meshtastic_Data positionData()
{
    runtime::MeshtasticPositionInput input{};
    input.valid = true;
    input.latitude_deg = 26.67773;
    input.longitude_deg = 107.28225;
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_POSITION_APP;
    size_t size = sizeof(data.payload.bytes);
    assert(runtime::MeshtasticPositionCore::buildPositionPayload(input, data.payload.bytes, &size));
    data.payload.size = static_cast<pb_size_t>(size);
    return data;
}
uint32_t bits(const meshtastic_Data& data)
{
    meshtastic_Position decoded = meshtastic_Position_init_zero;
    pb_istream_t input = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    assert(pb_decode(&input, meshtastic_Position_fields, &decoded));
    return decoded.precision_bits;
}
} // namespace

int main()
{
    auto& config = app::configFacade().settings;
    auto primary = positionData(), secondary = primary;
    assert(appConsumer(primary, ChannelId::PRIMARY));
    assert(appConsumer(secondary, ChannelId::SECONDARY));
    assert(bits(primary) == 13 && bits(secondary) == 19);
    assert(std::memcmp(primary.payload.bytes, secondary.payload.bytes, primary.payload.size) != 0);
    primary = positionData();
    secondary = primary;
    assert(packetConsumer(primary, 0));
    assert(packetConsumer(secondary, 1));
    assert(bits(primary) == 13 && bits(secondary) == 19);
    config.primary_channel_position_precision = 0;
    primary = positionData();
    secondary = primary;
    assert(!appConsumer(primary, ChannelId::PRIMARY));
    assert(!packetConsumer(primary, 0));
    assert(appConsumer(secondary, ChannelId::SECONDARY));
    assert(bits(secondary) == 19);
    config.primary_channel_has_module_settings = false;
    primary = positionData();
    const auto legacy = primary;
    assert(appConsumer(primary, ChannelId::PRIMARY));
    assert(std::memcmp(primary.payload.bytes, legacy.payload.bytes, legacy.payload.size) == 0);
    assert(packetConsumer(primary, 0));
    config.primary_channel_has_module_settings = true;
    config.primary_channel_position_precision = UINT32_MAX;
    primary = positionData();
    assert(!appConsumer(primary, ChannelId::PRIMARY));
    assert(!packetConsumer(primary, 0));
    primary.payload.size = 0;
    assert(appConsumer(primary, ChannelId::PRIMARY));
    assert(packetConsumer(primary, 0));
    primary.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    primary.payload.size = 3;
    std::memcpy(primary.payload.bytes, "abc", 3);
    assert(appConsumer(primary, ChannelId::PRIMARY));
    assert(packetConsumer(primary, 0));
    assert(std::memcmp(primary.payload.bytes, "abc", 3) == 0);
    std::cout << "Actual ESP app/packet consumers: primary/secondary, disabled/legacy/invalid, wire and MQTT payload agree. Position scalar bytes="
              << sizeof(meshtastic_Position) << '\n';
}
