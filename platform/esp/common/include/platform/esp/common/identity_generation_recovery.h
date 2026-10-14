#pragma once
#include "platform/esp/common/identity_atomic_record.h"

namespace platform::esp::common
{
// Called only after validating a complete committed identity record and deriving
// its identity hash. Recover a missed epoch update without ever rolling back a
// newer epoch or accepting two identities with the same generation.
inline bool recoverIdentityGeneration(const uint8_t identity_hash[16], uint32_t record_generation, uint32_t& generation)
{
    generation = 0;
    if (!identity_hash || !record_generation) return false;
    nvs_handle_t handle = 0;
    if (nvs_open("tm_author", NVS_READWRITE, &handle) != ESP_OK) return false;
    struct Close
    {
        nvs_handle_t handle;
        ~Close() { nvs_close(handle); }
    } close{handle};
    uint8_t epoch[20]{};
    size_t size = sizeof(epoch);
    const auto read = nvs_get_blob(handle, "epoch", epoch, &size);
    if (read == ESP_OK)
    {
        if (size != sizeof(epoch)) return false;
        const auto current = identityRecordU32(epoch);
        if (!current) return false;
        if (current >= record_generation)
        {
            if (std::memcmp(epoch + 4, identity_hash, 16)) return false;
            generation = current;
            return true;
        }
    }
    else if (read != ESP_ERR_NVS_NOT_FOUND) return false;
    identityRecordPutU32(epoch, record_generation);
    std::memcpy(epoch + 4, identity_hash, 16);
    if (nvs_set_blob(handle, "epoch", epoch, sizeof(epoch)) != ESP_OK || nvs_commit(handle) != ESP_OK) return false;
    generation = record_generation;
    return true;
}
} // namespace platform::esp::common
