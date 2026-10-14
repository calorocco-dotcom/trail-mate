#include "app/tms_c6_config_extension.h"
#include <cassert>
#include <string>
namespace tms = app::tms;
static bool decode(const std::string& text, tms::c6::Config& target)
{
    target = tms::c6::Config{};
    tms::Decoder decoder(
        nullptr, tms::DocumentKind::Working,
        [](void* context, const tms::RecordReader& record)
        { return tms::c6::consume(*static_cast<tms::c6::Config*>(context), record); },
        &target,
        [](void* context, bool, uint16_t)
        { return tms::c6::valid(*static_cast<tms::c6::Config*>(context)); });
    size_t offset = 0;
    while (offset < text.size())
    {
        const auto end = text.find('\n', offset);
        if (end == std::string::npos || end - offset >= tms::kMaxLineBytes) return false;
        char line[tms::kMaxLineBytes]{};
        std::memcpy(line, text.data() + offset, end - offset);
        if (!decoder.consumeLine(line)) return false;
        offset = end + 1;
    }
    return decoder.finish();
}
int main()
{
    app::AppConfig app;
    tms::c6::Config source, target;
    source.seen = tms::c6::kComplete;
    source.wifi_sta_enabled = true;
    assert(tms::c6::valid(source));
    source.wifi_enabled = true;
    assert(!tms::c6::valid(source));
    std::strcpy(source.wifi_sta_ssid, "Mountain % camp");
    assert(tms::c6::valid(source));
    source.wifi_ap_enabled = true;
    source.wifi_ap_ssid[0] = 0;
    assert(!tms::c6::valid(source));
    std::strcpy(source.wifi_ap_ssid, "TrailMate-C6");
    assert(tms::c6::valid(source));
    std::strcpy(source.wifi_ap_password, "1234567");
    assert(!tms::c6::valid(source));
    std::strcpy(source.wifi_ap_password, "12345678");
    assert(tms::c6::valid(source));
    source.wifi_enabled = false;
    std::strcpy(source.wifi_ap_password, "short");
    assert(tms::c6::valid(source));
    assert(!tms::c6::valid(source, true));
    std::strcpy(source.wifi_ap_password, "12345678");
    assert(tms::c6::valid(source, true));
    std::strcpy(source.wifi_sta_password, "private-value");
    std::string text;
    tms::LineScratch scratch;
    assert(tms::writeDocument(
        app, tms::DocumentKind::Working,
        {&text, [](void* out, const char* bytes, size_t size)
         { static_cast<std::string*>(out)->append(bytes, size); return true; }},
        scratch, nullptr,
        [](void* context, tms::RecordWriter& writer)
        { return tms::c6::write(*static_cast<tms::c6::Config*>(context), writer); },
        &source));
    assert(decode(text, target));
    assert(target.wifi_sta_enabled && !std::strcmp(target.wifi_sta_ssid, source.wifi_sta_ssid) && !std::strcmp(target.wifi_sta_password, source.wifi_sta_password));
    const auto line = std::string("c6_companion.wifi_ap_channel=u8:1\n");
    const auto position = text.find(line);
    assert(position != std::string::npos);
    auto bad = text;
    bad.replace(position, line.size(), "c6_companion.wifi_ap_channel=u8:0\n");
    assert(!decode(bad, target));
    bad = text;
    bad.erase(position, line.size());
    assert(!decode(bad, target));
    bad = text;
    bad.insert(position, line);
    assert(!decode(bad, target));
    bad = text;
    bad.replace(position, line.size(), "c6_companion.wifi_ap_channel=bool:1\n");
    assert(!decode(bad, target));
}
