#pragma once
#include <nvs.h>

namespace platform::esp::common
{
// Creation is permitted only for an absent identity. Corrupt, partial, empty,
// differently typed or unreadable keys must never silently re-key an account.
inline bool lxmfIdentityKeysAbsent()
{
    nvs_handle_t handle = 0;
    const auto result = nvs_open("lxmf_ident", NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return true;
    if (result != ESP_OK) return false;
    struct Close
    {
        nvs_handle_t handle;
        ~Close() { nvs_close(handle); }
    } close{handle};
    constexpr const char* keys[] = {"native_v1", "enc_pub", "enc_priv", "sig_pub", "sig_priv"};
    for (const auto* key : keys)
    {
        size_t length = 0;
        if (nvs_get_blob(handle, key, nullptr, &length) != ESP_ERR_NVS_NOT_FOUND) return false;
    }
    return true;
}
} // namespace platform::esp::common
