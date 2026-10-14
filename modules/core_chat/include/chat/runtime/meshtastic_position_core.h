#pragma once

#include <cstddef>
#include <cstdint>

#include "meshtastic/mesh.pb.h"

namespace chat::runtime
{

struct MeshtasticPositionInput
{
    bool valid = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    bool has_altitude = false;
    double altitude_m = 0.0;
    bool has_speed = false;
    double speed_mps = 0.0;
    bool has_course = false;
    double course_deg = 0.0;
    uint32_t satellites = 0;
    uint32_t timestamp_s = 0;
};

enum class MeshtasticPositionAvailabilityReason : uint8_t
{
    Available,
    InvalidFix,
    InvalidCoordinates,
};

struct MeshtasticPositionAvailability
{
    bool available = false;
    MeshtasticPositionAvailabilityReason reason =
        MeshtasticPositionAvailabilityReason::InvalidFix;
};

class MeshtasticPositionCore final
{
  public:
    static MeshtasticPositionAvailability resolveAvailability(
        const MeshtasticPositionInput& input);

    static bool buildPositionPayload(const MeshtasticPositionInput& input,
                                     uint8_t* out_buf,
                                     size_t* out_len);

    // Mutates caller-owned Data scratch before encoding/encryption. No heap or
    // resident storage. A false result must never be transmitted or mirrored.
    static bool applyChannelPrecision(meshtastic_Data& data,
                                      bool has_module_settings,
                                      uint32_t precision_bits);
};

} // namespace chat::runtime
