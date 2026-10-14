#include "platform/ui/settings_store.h"
#include <cassert>
#include <cstring>
#include <string>
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_ERR_NVS_TYPE_MISMATCH = 2, NVS_READONLY = 1;
int open_result = 0, measure_result = 0, read_result = 0;
unsigned closed = 0, reads = 0;
std::string stored = "secret";
bool wrong_length = false;
int nvs_open(const char*, int, nvs_handle_t* handle)
{
    *handle = 7;
    return open_result;
}
void nvs_close(nvs_handle_t handle)
{
    assert(handle == 7);
    closed++;
}
int nvs_get_str(nvs_handle_t, const char*, char* out, size_t* size)
{
    if (!out)
    {
        *size = stored.size() + 1;
        return measure_result;
    }
    reads++;
    assert(*size >= stored.size() + 1);
    std::memcpy(out, stored.c_str(), stored.size() + 1);
    *size = stored.size() + 1 + (wrong_length ? 1 : 0);
    return read_result;
}
namespace platform::esp::common::settings_store_detail
{
const char* resolve_storage_key(const char* key) { return key; }
bool validate_storage_key(const char*, const char*, const char*, const char*) { return true; }
} // namespace platform::esp::common::settings_store_detail
namespace platform::ui::settings_store
{
#include "settings_string_read_actual.inc"
}
int main()
{
    using namespace platform::ui::settings_store;
    char out[64]{};
    size_t length = 999;
    const auto call = [&]
    { return read_string_into("c6_companion", "wifi_sta_password", out, sizeof(out), &length); };
    assert(call() == StringReadResult::Ok && length == 6 && !std::strcmp(out, "secret") && closed == 1);
    open_result = ESP_ERR_NVS_NOT_FOUND;
    assert(call() == StringReadResult::Missing && length == 0 && out[0] == 0 && closed == 1);
    open_result = 9;
    assert(call() == StringReadResult::Unavailable && closed == 1);
    open_result = 0;
    measure_result = ESP_ERR_NVS_NOT_FOUND;
    assert(call() == StringReadResult::Missing && closed == 2);
    measure_result = ESP_ERR_NVS_TYPE_MISMATCH;
    assert(call() == StringReadResult::Invalid && closed == 3);
    measure_result = 9;
    assert(call() == StringReadResult::Unavailable && closed == 4);
    measure_result = 0;
    stored = std::string(64, 'x');
    const auto before = reads;
    assert(call() == StringReadResult::Invalid && reads == before && closed == 5);
    stored = "secret";
    read_result = 9;
    assert(call() == StringReadResult::Unavailable && out[0] == 0 && length == 0);
    read_result = 0;
    wrong_length = true;
    assert(call() == StringReadResult::Invalid);
    wrong_length = false;
    stored = std::string("a\0b", 3);
    assert(call() == StringReadResult::Invalid && out[0] == 0);
    stored = "";
    assert(call() == StringReadResult::Ok && length == 0);
    assert(read_string_into(nullptr, "key", out, sizeof(out), &length) == StringReadResult::Invalid);
}
