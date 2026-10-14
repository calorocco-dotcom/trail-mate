#include "platform/ui/settings_store.h"

#include <cassert>
#include <cstring>
#include <vector>

using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_ERR_NVS_TYPE_MISMATCH = 2,
              ESP_ERR_NVS_INVALID_LENGTH = 3, NVS_READONLY = 1;
int open_result = 0, measure_result = 0, read_result = 0;
unsigned closed = 0, reads = 0;
bool wrong_length = false;
std::vector<uint8_t> stored = {1, 0, 3};
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
int nvs_get_blob(nvs_handle_t, const char*, void* out, std::size_t* size)
{
    if (!out)
    {
        *size = stored.size();
        return measure_result;
    }
    ++reads;
    assert(*size >= stored.size());
    std::memcpy(out, stored.data(), stored.size());
    *size = stored.size() + (wrong_length ? 1 : 0);
    return read_result;
}
namespace platform::esp::common::settings_store_detail
{
const char* resolve_storage_key(const char* key) { return key; }
bool validate_storage_key(const char*, const char*, const char*, const char*) { return true; }
} // namespace platform::esp::common::settings_store_detail
namespace platform::ui::settings_store
{
#include "settings_blob_read_actual.inc"
}
int main()
{
    using namespace platform::ui::settings_store;
    uint8_t out[4]{};
    std::size_t size = 999;
    const auto call = [&]
    { return read_blob_into("settings", "timezone_tzdef", out, sizeof(out), &size); };
    assert(call() == BlobReadResult::Ok && size == 3 && out[1] == 0 && closed == 1);
    open_result = ESP_ERR_NVS_NOT_FOUND;
    assert(call() == BlobReadResult::Missing && size == 0 && closed == 1);
    open_result = 9;
    assert(call() == BlobReadResult::Unavailable && closed == 1);
    open_result = 0;
    measure_result = ESP_ERR_NVS_NOT_FOUND;
    assert(call() == BlobReadResult::Missing && size == 0 && closed == 2);
    measure_result = ESP_ERR_NVS_TYPE_MISMATCH;
    assert(call() == BlobReadResult::Invalid && closed == 3);
    measure_result = 9;
    assert(call() == BlobReadResult::Unavailable && closed == 4);
    measure_result = 0;
    stored.assign(5, 1);
    const auto before = reads;
    assert(call() == BlobReadResult::Invalid && reads == before && size == 0 && closed == 5);
    stored = {1, 0, 3};
    for (const int failure : {9, ESP_ERR_NVS_INVALID_LENGTH, ESP_ERR_NVS_TYPE_MISMATCH})
    {
        read_result = failure;
        assert(call() == (failure == 9 ? BlobReadResult::Unavailable : BlobReadResult::Invalid));
        assert(size == 0 && out[0] == 0 && out[1] == 0 && out[2] == 0);
    }
    read_result = 0;
    wrong_length = true;
    assert(call() == BlobReadResult::Invalid && size == 0 && out[0] == 0);
    wrong_length = false;
    stored.clear();
    assert(call() == BlobReadResult::Ok && size == 0);
    assert(read_blob_into("settings", "key", nullptr, 0, &size) == BlobReadResult::Ok);
    assert(read_blob_into(nullptr, "key", out, sizeof(out), &size) == BlobReadResult::Invalid);
    assert(read_blob_into("settings", "key", nullptr, 1, &size) == BlobReadResult::Invalid);
}
