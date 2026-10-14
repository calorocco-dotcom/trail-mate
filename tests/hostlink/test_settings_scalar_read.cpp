#include "platform/ui/settings_store.h"
#include <cassert>
#include <climits>

using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_ERR_NVS_TYPE_MISMATCH = 2, NVS_READONLY = 1;
int open_result = 0, read_result = 0;
unsigned closed = 0, reads = 0;
int32_t signed_value = -420;
uint32_t unsigned_value = UINT32_MAX;
uint8_t bool_value = 1;
bool valid_key = true;
int nvs_open(const char*, int, nvs_handle_t* handle)
{
    *handle = 7;
    return open_result;
}
void nvs_close(nvs_handle_t handle)
{
    assert(handle == 7);
    ++closed;
}
int nvs_get_i32(nvs_handle_t, const char*, int32_t* out)
{
    ++reads;
    *out = signed_value;
    return read_result;
}
int nvs_get_u32(nvs_handle_t, const char*, uint32_t* out)
{
    ++reads;
    *out = unsigned_value;
    return read_result;
}
int nvs_get_u8(nvs_handle_t, const char*, uint8_t* out)
{
    ++reads;
    *out = bool_value;
    return read_result;
}
namespace platform::esp::common::settings_store_detail
{
const char* resolve_storage_key(const char* key) { return key; }
bool validate_storage_key(const char*, const char*, const char*, const char*) { return valid_key; }
} // namespace platform::esp::common::settings_store_detail
namespace platform::ui::settings_store
{
#include "settings_scalar_read_actual.inc"
}
int main()
{
    using namespace platform::ui::settings_store;
    int32_t integer = 0;
    uint32_t number = 0;
    bool flag = false;
    for (int failure : {ESP_OK, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_NVS_TYPE_MISMATCH, 9})
    {
        read_result = failure;
        const auto expected = failure == ESP_OK ? ScalarReadResult::Ok : failure == ESP_ERR_NVS_NOT_FOUND   ? ScalarReadResult::Missing
                                                                     : failure == ESP_ERR_NVS_TYPE_MISMATCH ? ScalarReadResult::Invalid
                                                                                                            : ScalarReadResult::Unavailable;
        const auto before = closed;
        assert(read_int_into("settings", "timezone_offset", &integer) == expected);
        assert(read_uint_into("power", "gauge_full_mah", &number) == expected);
        assert(read_bool_into("settings", "adv_debug", &flag) == expected);
        assert(closed == before + 3);
        assert(integer == (failure == ESP_OK ? -420 : 0));
        assert(number == (failure == ESP_OK ? UINT32_MAX : 0));
        assert(flag == (failure == ESP_OK));
    }
    read_result = 0;
    for (int failure : {ESP_ERR_NVS_NOT_FOUND, 9})
    {
        open_result = failure;
        const auto before = reads, close_before = closed;
        assert(read_uint_into("power", "gauge_full_mah", &number) ==
               (failure == ESP_ERR_NVS_NOT_FOUND ? ScalarReadResult::Missing : ScalarReadResult::Unavailable));
        assert(number == 0 && reads == before && closed == close_before);
    }
    open_result = 0;
    for (auto value : {INT32_MIN, -1, 0, INT32_MAX})
    {
        signed_value = value;
        assert(read_int_into("settings", "timezone_offset", &integer) == ScalarReadResult::Ok && integer == value);
    }
    const uint32_t unsigned_values[] = {0, 1, UINT32_MAX};
    for (uint32_t value : unsigned_values)
    {
        unsigned_value = value;
        assert(read_uint_into("power", "gauge_full_mah", &number) == ScalarReadResult::Ok && number == value);
    }
    for (uint8_t value : {0, 1, 2, 255})
    {
        bool_value = value;
        uint8_t byte = 0;
        assert(read_u8_into("mc_ble", "advert_loc", &byte) == ScalarReadResult::Ok && byte == value);
        assert(read_bool_into("settings", "adv_debug", &flag) ==
               (value <= 1 ? ScalarReadResult::Ok : ScalarReadResult::Invalid));
        assert(flag == (value == 1));
    }
    assert(read_int_into(nullptr, "key", &integer) == ScalarReadResult::Invalid);
    assert(read_uint_into("settings", nullptr, &number) == ScalarReadResult::Invalid);
    assert(read_bool_into("settings", "key", nullptr) == ScalarReadResult::Invalid);
    valid_key = false;
    assert(read_int_into("settings", "key", &integer) == ScalarReadResult::Invalid);
}
