#include "chat/runtime/meshtastic_position_core.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>

int main()
{
    {
        chat::runtime::MeshtasticPositionInput input{};
        input.valid = true;
        input.latitude_deg = 26.67773;
        input.longitude_deg = 107.28225;
        input.has_altitude = true;
        input.altitude_m = 1903.6;
        input.has_speed = true;
        input.speed_mps = 3.6;
        input.has_course = true;
        input.course_deg = 360.0;
        input.satellites = 11;
        input.timestamp_s = 1710000000U;
        const auto available =
            chat::runtime::MeshtasticPositionCore::resolveAvailability(input);
        assert(available.available);
        assert(available.reason == chat::runtime::MeshtasticPositionAvailabilityReason::Available);

        uint8_t payload[96] = {};
        size_t payload_len = sizeof(payload);
        assert(chat::runtime::MeshtasticPositionCore::buildPositionPayload(input,
                                                                           payload,
                                                                           &payload_len));
        assert(payload_len > 0);

        meshtastic_Position pos = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);
        assert(pb_decode(&stream, meshtastic_Position_fields, &pos));
        assert(pos.has_latitude_i);
        assert(pos.latitude_i == static_cast<int32_t>(input.latitude_deg * 1e7));
        assert(pos.has_longitude_i);
        assert(pos.longitude_i == static_cast<int32_t>(input.longitude_deg * 1e7));
        assert(pos.location_source == meshtastic_Position_LocSource_LOC_INTERNAL);
        assert(pos.has_altitude);
        assert(pos.altitude == 1904);
        assert(pos.altitude_source == meshtastic_Position_AltSource_ALT_INTERNAL);
        assert(pos.has_ground_speed);
        assert(pos.ground_speed == 4U);
        assert(pos.has_ground_track);
        assert(pos.ground_track == 35999U);
        assert(pos.sats_in_view == 11U);
        assert(pos.timestamp == input.timestamp_s);
    }

    {
        chat::runtime::MeshtasticPositionInput input{};
        input.valid = false;
        input.latitude_deg = 26.0;
        input.longitude_deg = 107.0;
        const auto available =
            chat::runtime::MeshtasticPositionCore::resolveAvailability(input);
        assert(!available.available);
        assert(available.reason == chat::runtime::MeshtasticPositionAvailabilityReason::InvalidFix);

        uint8_t payload[96] = {};
        size_t payload_len = sizeof(payload);
        assert(!chat::runtime::MeshtasticPositionCore::buildPositionPayload(input,
                                                                            payload,
                                                                            &payload_len));
    }

    {
        chat::runtime::MeshtasticPositionInput input{};
        input.valid = true;
        input.latitude_deg = std::numeric_limits<double>::quiet_NaN();
        input.longitude_deg = 107.0;

        const auto available =
            chat::runtime::MeshtasticPositionCore::resolveAvailability(input);
        assert(!available.available);
        assert(available.reason ==
               chat::runtime::MeshtasticPositionAvailabilityReason::InvalidCoordinates);
    }

    // Decode real protobuf output rather than comparing the implementation's
    // own intermediate values. Exercise signs, world edges and all bit counts.
    for (const double latitude : {-90.0, -26.67773, 0.0, 26.67773, 90.0})
    {
        for (const double longitude : {-180.0, -107.28225, 0.0, 107.28225, 180.0})
        {
            for (uint32_t bits = 1; bits <= 32; ++bits)
            {
                chat::runtime::MeshtasticPositionInput input{};
                input.valid = true;
                input.latitude_deg = latitude;
                input.longitude_deg = longitude;
                input.has_altitude = true;
                input.altitude_m = 1903;
                input.timestamp_s = 1710000000U;
                meshtastic_Data data = meshtastic_Data_init_zero;
                data.portnum = meshtastic_PortNum_POSITION_APP;
                data.request_id = 123;
                data.want_response = true;
                size_t size = sizeof(data.payload.bytes);
                assert(chat::runtime::MeshtasticPositionCore::buildPositionPayload(input, data.payload.bytes, &size));
                data.payload.size = static_cast<pb_size_t>(size);
                const auto original = data;
                assert(chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, false, 0));
                assert(data.payload.size == original.payload.size);
                assert(std::memcmp(data.payload.bytes, original.payload.bytes, size) == 0);
                assert(chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, bits));
                meshtastic_Position decoded = meshtastic_Position_init_zero;
                pb_istream_t stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
                assert(pb_decode(&stream, meshtastic_Position_fields, &decoded));
                assert(decoded.precision_bits == bits);
                assert(decoded.has_latitude_i && decoded.has_longitude_i);
                assert(decoded.latitude_i >= -900000000 && decoded.latitude_i <= 900000000);
                assert(decoded.longitude_i >= -1800000000 && decoded.longitude_i <= 1800000000);
                assert(decoded.has_altitude && decoded.altitude == 1903);
                assert(decoded.timestamp == input.timestamp_s);
                assert(data.request_id == 123 && data.want_response);
                if (bits == 32)
                {
                    assert(decoded.latitude_i == static_cast<int32_t>(latitude * 1e7));
                    assert(decoded.longitude_i == static_cast<int32_t>(longitude * 1e7));
                }
                else
                {
                    const int64_t width = int64_t{1} << (32 - bits);
                    assert(std::abs(int64_t{decoded.latitude_i} - static_cast<int32_t>(latitude * 1e7)) <= width);
                    assert(std::abs(int64_t{decoded.longitude_i} - static_cast<int32_t>(longitude * 1e7)) <= width);
                }
                const auto quantized = data;
                assert(chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, bits));
                assert(data.payload.size == quantized.payload.size);
                assert(std::memcmp(data.payload.bytes, quantized.payload.bytes, data.payload.size) == 0);
                assert(chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 32));
                stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
                assert(pb_decode(&stream, meshtastic_Position_fields, &decoded));
                assert(decoded.precision_bits == bits); // never relabel coarse input as precise
                assert(!chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 0));
                assert(!chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 33));
                assert(!chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, UINT32_MAX));
            }
        }
    }
    {
        meshtastic_Data data = meshtastic_Data_init_zero;
        data.portnum = meshtastic_PortNum_POSITION_APP;
        assert(chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 0)); // empty request
        const uint8_t latitude_only[] = {0x0d, 0, 0, 0, 0};
        std::memcpy(data.payload.bytes, latitude_only, sizeof(latitude_only));
        data.payload.size = sizeof(latitude_only);
        assert(!chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 0));
        data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        assert(chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 0));
        assert(std::memcmp(data.payload.bytes, latitude_only, sizeof(latitude_only)) == 0);
        data.portnum = meshtastic_PortNum_POSITION_APP;
        data.payload.size = 1;
        data.payload.bytes[0] = 0x0d; // truncated sfixed32
        assert(!chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 13));
        data.payload.size = sizeof(data.payload.bytes) + 1;
        assert(!chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 13));
    }
    {
        meshtastic_Position input = meshtastic_Position_init_zero;
        input.has_latitude_i = input.has_longitude_i = true;
        input.latitude_i = -266777300;
        input.longitude_i = -1072822500;
        meshtastic_Data data = meshtastic_Data_init_zero;
        data.portnum = meshtastic_PortNum_POSITION_APP;
        pb_ostream_t output = pb_ostream_from_buffer(data.payload.bytes, sizeof(data.payload.bytes));
        assert(pb_encode(&output, meshtastic_Position_fields, &input));
        data.payload.size = static_cast<pb_size_t>(output.bytes_written);
        assert(chat::runtime::MeshtasticPositionCore::applyChannelPrecision(data, true, 13));
        pb_istream_t stream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
        assert(pb_decode(&stream, meshtastic_Position_fields, &input));
        assert(input.latitude_i == -266600448);
        assert(input.longitude_i == -1072955392);
    }
    return 0;
}
