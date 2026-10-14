#pragma once
#include <cstdint>
#include <cstring>
#include <nvs.h>

namespace platform::esp::common
{
// One atomic NVS blob: LE generation32 + public identity hash16. No private
// material or heap allocation. A malformed record never resets the counter.
inline bool syncIdentityGeneration(const uint8_t identity_hash[16], uint32_t& generation,
                                   bool force_advance = false)
{
    generation = 0;
    if (!identity_hash) return false;
    nvs_handle_t handle = 0;
    if (nvs_open("tm_author", NVS_READWRITE, &handle) != ESP_OK) return false;
    struct Close
    {
        nvs_handle_t handle;
        ~Close() { nvs_close(handle); }
    } close{handle};
    uint8_t record[20]{};
    size_t size = sizeof(record);
    const auto result = nvs_get_blob(handle, "epoch", record, &size);
    uint32_t next = 1;
    if (result == ESP_OK)
    {
        if (size != sizeof(record)) return false;
        next = static_cast<uint32_t>(record[0]) | (static_cast<uint32_t>(record[1]) << 8) |
               (static_cast<uint32_t>(record[2]) << 16) | (static_cast<uint32_t>(record[3]) << 24);
        if (!next) return false;
        if (!force_advance && !std::memcmp(record + 4, identity_hash, 16))
        {
            generation = next;
            return true;
        }
        if (next == UINT32_MAX) return false;
        ++next;
    }
    else if (result != ESP_ERR_NVS_NOT_FOUND) return false;
    for (unsigned index = 0; index < 4; ++index) record[index] = static_cast<uint8_t>(next >> (index * 8));
    std::memcpy(record + 4, identity_hash, 16);
    if (nvs_set_blob(handle, "epoch", record, sizeof(record)) != ESP_OK || nvs_commit(handle) != ESP_OK) return false;
    generation = next;
    return true;
}
} // namespace platform::esp::common
