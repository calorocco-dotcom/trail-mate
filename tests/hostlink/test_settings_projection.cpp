#include "app/tms_config_codec.h"
#include "platform/ui/settings_store.h"

#include <cassert>
#include <cstring>
#include <string>

namespace platform::ui::settings_store
{
StringReadResult text_result = StringReadResult::Ok;
BlobReadResult blob_result = BlobReadResult::Ok;
std::string text = "pinyin,latin";
uint8_t blob[] = {1, 0, 3};
StringReadResult read_string_into(const char*, const char*, char* out,
                                  std::size_t capacity, std::size_t* length)
{
    *length = 0;
    out[0] = 0;
    if (text_result != StringReadResult::Ok) return text_result;
    if (text.size() >= capacity) return StringReadResult::Invalid;
    std::memcpy(out, text.c_str(), text.size() + 1);
    *length = text.size();
    return StringReadResult::Ok;
}
bool get_string_into(const char* ns, const char* key, char* out,
                     std::size_t capacity, std::size_t* length)
{
    return read_string_into(ns, key, out, capacity, length) == StringReadResult::Ok;
}
BlobReadResult read_blob_into(const char*, const char*, void* out,
                              std::size_t capacity, std::size_t* length)
{
    *length = 0;
    if (blob_result != BlobReadResult::Ok) return blob_result;
    assert(capacity >= sizeof(blob));
    std::memcpy(out, blob, sizeof(blob));
    *length = sizeof(blob);
    return BlobReadResult::Ok;
}
bool get_blob_into(const char* ns, const char* key, void* out,
                   std::size_t capacity, std::size_t* length)
{
    return read_blob_into(ns, key, out, capacity, length) == BlobReadResult::Ok;
}
} // namespace platform::ui::settings_store

namespace
{
namespace tms = app::tms;
struct State
{
    uint8_t timezone_tzdef[192];
} state;
State* s_state_storage = &state;
#define s_state (*s_state_storage)
#include "settings_projection_actual.inc"
#undef s_state
bool append(void* context, const char* bytes, std::size_t size)
{
    static_cast<std::string*>(context)->append(bytes, size);
    return true;
}
} // namespace
int main()
{
    using namespace platform::ui::settings_store;
    std::string output;
    app::tms::LineScratch scratch{};
    app::tms::RecordWriter writer({&output, append}, scratch, nullptr);
    char text_scratch[48];
    for (auto result : {StringReadResult::Ok, StringReadResult::Missing,
                        StringReadResult::Invalid, StringReadResult::Unavailable})
    {
        text_result = result;
        output.clear();
        const bool accepted = result == StringReadResult::Ok || result == StringReadResult::Missing;
        assert(write_text_setting(writer, "ui.locale", "settings", "display_locale",
                                  text_scratch, sizeof(text_scratch)) == accepted);
        assert(!accepted ? output.empty() : output == (result == StringReadResult::Ok ? "ui.locale=str:pinyin%2Clatin\n" : "ui.locale=str:\n"));
        output.clear();
        assert(write_string_as_blob(writer, "ui.ime.enabled", "settings", "enabled_imes", 191) == accepted);
        assert(!accepted ? output.empty() : output == (result == StringReadResult::Ok ? "ui.ime.enabled=b64:cGlueWluLGxhdGlu\n" : "ui.ime.enabled=b64:\n"));
    }
    text_result = StringReadResult::Ok;
    text.assign(48, 'x');
    output.clear();
    assert(!write_text_setting(writer, "ui.locale", "settings", "display_locale",
                               text_scratch, sizeof(text_scratch)) &&
           output.empty());
    text.assign(192, 'x');
    assert(!write_string_as_blob(writer, "ui.ime.enabled", "settings", "enabled_imes", 191) && output.empty());
    for (auto result : {BlobReadResult::Ok, BlobReadResult::Missing,
                        BlobReadResult::Invalid, BlobReadResult::Unavailable})
    {
        blob_result = result;
        output.clear();
        const bool accepted = result == BlobReadResult::Ok || result == BlobReadResult::Missing;
        assert(write_blob_setting(writer, "ui.timezone.tzdef", "settings", "timezone_tzdef") == accepted);
        assert(!accepted ? output.empty() : output == (result == BlobReadResult::Ok ? "ui.timezone.tzdef=b64:AQAD\n" : "ui.timezone.tzdef=b64:\n"));
    }
}
