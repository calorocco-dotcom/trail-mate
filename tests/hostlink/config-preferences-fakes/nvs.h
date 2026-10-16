#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <variant>
#include <vector>
using esp_err_t = int;
using nvs_handle_t = uint32_t;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READONLY = 0, NVS_READWRITE = 1;
constexpr int ESP_ERR_NVS_TYPE_MISMATCH = 3, ESP_ERR_NVS_INVALID_LENGTH = 5;
namespace fake_nvs
{
using Value = std::variant<uint8_t, uint32_t, int32_t, std::string, std::vector<uint8_t>>;
inline std::map<std::string, std::map<std::string, Value>> values;
inline std::map<nvs_handle_t, std::pair<std::string, bool>> handles;
inline nvs_handle_t next = 1;
inline bool fail_commit = false;
inline int open_error = 0;
inline unsigned commits = 0;
template <typename T>
int get(nvs_handle_t handle, const char* key, T* output)
{
    if (!handles.count(handle)) return 2;
    const auto& records = values[handles[handle].first];
    const auto record = records.find(key);
    if (record == records.end()) return ESP_ERR_NVS_NOT_FOUND;
    const auto* value = std::get_if<T>(&record->second);
    if (!value) return 3;
    *output = *value;
    return ESP_OK;
}
template <typename T>
int put(nvs_handle_t handle, const char* key, T value)
{
    if (!handles.count(handle) || !handles[handle].second) return 2;
    values[handles[handle].first][key] = value;
    return ESP_OK;
}
} // namespace fake_nvs
inline int nvs_open(const char* ns, int mode, nvs_handle_t* output)
{
    if (fake_nvs::open_error) return fake_nvs::open_error;
    if (mode == NVS_READONLY && !fake_nvs::values.count(ns)) return ESP_ERR_NVS_NOT_FOUND;
    fake_nvs::values[ns];
    *output = fake_nvs::next++;
    fake_nvs::handles[*output] = {ns, mode == NVS_READWRITE};
    return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) { fake_nvs::handles.erase(handle); }
inline int nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* output) { return fake_nvs::get(handle, key, output); }
inline int nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* output) { return fake_nvs::get(handle, key, output); }
inline int nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) { return fake_nvs::put(handle, key, value); }
inline int nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value) { return fake_nvs::put(handle, key, value); }
inline int nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* output) { return fake_nvs::get(handle, key, output); }
inline int nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value) { return fake_nvs::put(handle, key, value); }
inline int nvs_set_str(nvs_handle_t handle, const char* key, const char* value) { return fake_nvs::put(handle, key, std::string(value)); }
inline int nvs_get_str(nvs_handle_t handle, const char* key, char* output, std::size_t* size)
{
    std::string value;
    const auto result = fake_nvs::get(handle, key, &value);
    if (result != ESP_OK) return result;
    const auto needed = value.size() + 1;
    if (output && *size < needed)
    {
        *size = needed;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (output) std::memcpy(output, value.c_str(), needed);
    *size = needed;
    return ESP_OK;
}
inline int nvs_erase_all(nvs_handle_t handle)
{
    if (!fake_nvs::handles.count(handle) || !fake_nvs::handles[handle].second) return 2;
    fake_nvs::values[fake_nvs::handles[handle].first].clear();
    return ESP_OK;
}
inline int nvs_commit(nvs_handle_t)
{
    ++fake_nvs::commits;
    return fake_nvs::fail_commit ? 4 : ESP_OK;
}
inline int nvs_get_blob(nvs_handle_t handle, const char* key, void* output, std::size_t* size)
{
    std::vector<uint8_t> value;
    const auto result = fake_nvs::get(handle, key, &value);
    if (result != ESP_OK) return result;
    if (output && *size < value.size())
    {
        *size = value.size();
        return 5;
    }
    if (output) std::memcpy(output, value.data(), value.size());
    *size = value.size();
    return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, std::size_t size)
{
    const auto* raw = static_cast<const uint8_t*>(value);
    return fake_nvs::put(handle, key, std::vector<uint8_t>(raw, raw + size));
}
inline int nvs_erase_key(nvs_handle_t handle, const char* key)
{
    if (!fake_nvs::handles.count(handle) || !fake_nvs::handles[handle].second) return 2;
    return fake_nvs::values[fake_nvs::handles[handle].first].erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
