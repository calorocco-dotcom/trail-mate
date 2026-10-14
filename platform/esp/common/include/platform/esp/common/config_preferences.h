#pragma once

// Preserve Arduino's existing typed NVS representation. Native IDF uses the
// same namespaces, keys and scalar types without an Arduino runtime dependency.
#if defined(ARDUINO)
#include <Preferences.h>
namespace platform::esp::common
{
using ConfigPreferences = ::Preferences;
}
#else
#include <cstddef>
#include <cstdint>
#include <nvs.h>

namespace platform::esp::common
{
class ConfigPreferences
{
  public:
    ~ConfigPreferences() { end(); }
    ConfigPreferences() = default;
    ConfigPreferences(const ConfigPreferences&) = delete;
    ConfigPreferences& operator=(const ConfigPreferences&) = delete;
    bool begin(const char* ns, bool read_only)
    {
        end();
        read_only_ = read_only;
        return ns && nvs_open(ns, read_only ? NVS_READONLY : NVS_READWRITE, &handle_) == ESP_OK;
    }
    void end()
    {
        if (handle_) nvs_close(handle_);
        handle_ = 0;
    }
    uint8_t getUChar(const char* key, uint8_t fallback = 0) const
    {
        uint8_t value;
        return handle_ && nvs_get_u8(handle_, key, &value) == ESP_OK ? value : fallback;
    }
    bool getBool(const char* key, bool fallback = false) const { return getUChar(key, fallback ? 1 : 0) != 0; }
    uint32_t getUInt(const char* key, uint32_t fallback = 0) const
    {
        uint32_t value;
        return handle_ && nvs_get_u32(handle_, key, &value) == ESP_OK ? value : fallback;
    }
    std::size_t getBytes(const char* key, void* output, std::size_t capacity) const
    {
        std::size_t size = capacity;
        return handle_ && nvs_get_blob(handle_, key, output, &size) == ESP_OK ? size : 0;
    }
    std::size_t putUChar(const char* key, uint8_t value) { return writable() && committed(nvs_set_u8(handle_, key, value)) ? 1 : 0; }
    std::size_t putBool(const char* key, bool value) { return putUChar(key, value ? 1 : 0); }
    std::size_t putUInt(const char* key, uint32_t value) { return writable() && committed(nvs_set_u32(handle_, key, value)) ? sizeof(value) : 0; }
    std::size_t putBytes(const char* key, const void* value, std::size_t size)
    {
        return writable() && committed(nvs_set_blob(handle_, key, value, size)) ? size : 0;
    }
    bool remove(const char* key)
    {
        if (!writable()) return false;
        const esp_err_t result = nvs_erase_key(handle_, key);
        return result == ESP_ERR_NVS_NOT_FOUND || committed(result);
    }

  private:
    bool writable() const { return handle_ && !read_only_; }
    bool committed(esp_err_t result) { return result == ESP_OK && nvs_commit(handle_) == ESP_OK; }
    nvs_handle_t handle_ = 0;
    bool read_only_ = true;
};
} // namespace platform::esp::common
#endif
