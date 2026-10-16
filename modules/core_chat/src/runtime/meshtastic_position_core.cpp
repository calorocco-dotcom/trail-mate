#include "chat/runtime/meshtastic_position_core.h"

#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <algorithm>
#include <cmath>

namespace chat::runtime
{
namespace
{

constexpr uint32_t kMinimumValidEpochSeconds = 1577836800U;

int32_t precisionCellCenter(int32_t coordinate, uint32_t precision, int64_t bound)
{
    const int64_t width = int64_t{1} << (32 - precision);
    const int64_t value = coordinate;
    // Floor division is required for southern/western coordinates. Intersect
    // edge cells with the physical coordinate range before taking the center.
    const int64_t cell = (value / width - (value < 0 && value % width != 0 ? 1 : 0)) * width;
    const int64_t lower = std::max(cell, -bound);
    const int64_t upper = std::min(cell + width, bound);
    return static_cast<int32_t>((lower + upper) / 2);
}

} // namespace

MeshtasticPositionAvailability MeshtasticPositionCore::resolveAvailability(
    const MeshtasticPositionInput& input)
{
    if (!input.valid)
    {
        MeshtasticPositionAvailability availability{};
        availability.available = false;
        availability.reason = MeshtasticPositionAvailabilityReason::InvalidFix;
        return availability;
    }
    if (!std::isfinite(input.latitude_deg) || !std::isfinite(input.longitude_deg))
    {
        MeshtasticPositionAvailability availability{};
        availability.available = false;
        availability.reason = MeshtasticPositionAvailabilityReason::InvalidCoordinates;
        return availability;
    }
    MeshtasticPositionAvailability availability{};
    availability.available = true;
    availability.reason = MeshtasticPositionAvailabilityReason::Available;
    return availability;
}

bool MeshtasticPositionCore::buildPositionPayload(const MeshtasticPositionInput& input,
                                                  uint8_t* out_buf,
                                                  size_t* out_len)
{
    const auto availability = resolveAvailability(input);
    if (!availability.available || !out_buf || !out_len || *out_len == 0)
    {
        return false;
    }

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.latitude_i = static_cast<int32_t>(input.latitude_deg * 1e7);
    pos.has_longitude_i = true;
    pos.longitude_i = static_cast<int32_t>(input.longitude_deg * 1e7);
    pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

    if (input.has_altitude && std::isfinite(input.altitude_m))
    {
        pos.has_altitude = true;
        pos.altitude = static_cast<int32_t>(std::lround(input.altitude_m));
        pos.altitude_source = meshtastic_Position_AltSource_ALT_INTERNAL;
    }
    if (input.has_speed && std::isfinite(input.speed_mps))
    {
        pos.has_ground_speed = true;
        pos.ground_speed = static_cast<uint32_t>(std::lround(input.speed_mps));
    }
    if (input.has_course && std::isfinite(input.course_deg))
    {
        double course = input.course_deg;
        if (course < 0.0)
        {
            course = 0.0;
        }
        uint32_t cdeg = static_cast<uint32_t>(std::lround(course * 100.0));
        if (cdeg >= 36000U)
        {
            cdeg = 35999U;
        }
        pos.has_ground_track = true;
        pos.ground_track = cdeg;
    }
    if (input.satellites > 0)
    {
        pos.sats_in_view = static_cast<uint32_t>(input.satellites);
    }

    if (input.timestamp_s >= kMinimumValidEpochSeconds)
    {
        pos.timestamp = input.timestamp_s;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out_buf, *out_len);
    if (!pb_encode(&stream, meshtastic_Position_fields, &pos))
    {
        return false;
    }

    *out_len = stream.bytes_written;
    return true;
}

bool MeshtasticPositionCore::applyChannelPrecision(meshtastic_Data& data,
                                                   bool has_module_settings,
                                                   uint32_t precision_bits)
{
    if (data.portnum != meshtastic_PortNum_POSITION_APP || !has_module_settings)
    {
        return true;
    }
    if (data.payload.size > sizeof(data.payload.bytes))
    {
        return false;
    }
    // Position is a small bounded scalar message, not a MeshPacket/Data/config
    // frame. Encoding uses the existing caller-owned payload buffer.
    static_assert(sizeof(meshtastic_Position) <= 128, "Position scalar stack budget");
    meshtastic_Position position = meshtastic_Position_init_zero;
    pb_istream_t input = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (!pb_decode(&input, meshtastic_Position_fields, &position))
    {
        return false;
    }
    // Empty requests and time-only messages do not reveal a position and must
    // remain available when local position sharing is disabled.
    if (!position.has_latitude_i && !position.has_longitude_i)
    {
        return true;
    }
    if (precision_bits == 0 || precision_bits > 32 || position.precision_bits > 32 ||
        (position.has_latitude_i && (position.latitude_i < -900000000 || position.latitude_i > 900000000)) ||
        (position.has_longitude_i && (position.longitude_i < -1800000000 || position.longitude_i > 1800000000)))
    {
        return false;
    }
    // Do not claim more detail than an already-quantized input declared.
    const uint32_t effective = position.precision_bits == 0
                                   ? precision_bits
                                   : std::min(precision_bits, position.precision_bits);
    if (effective < 32)
    {
        if (position.has_latitude_i)
        {
            position.latitude_i = precisionCellCenter(position.latitude_i, effective, 900000000);
        }
        if (position.has_longitude_i)
        {
            position.longitude_i = precisionCellCenter(position.longitude_i, effective, 1800000000);
        }
    }
    position.precision_bits = effective;
    pb_ostream_t output = pb_ostream_from_buffer(data.payload.bytes, sizeof(data.payload.bytes));
    if (!pb_encode(&output, meshtastic_Position_fields, &position))
    {
        return false;
    }
    data.payload.size = static_cast<pb_size_t>(output.bytes_written);
    return true;
}

} // namespace chat::runtime
