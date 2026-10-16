// Native test bridge only. The browser test supplies synthetic configurations;
// this does not open a device or store secrets/configuration on disk.
#include "app/tms_config_codec.h"
#if defined(TMS_COMPLETE_INTEROP)
#include "platform/esp/arduino_common/app_config_tms_settings_extension.h"
void resetCompleteConfigFixture();
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
constexpr uint32_t kMaxDocument = 32768;

bool append(void* context, const char* bytes, std::size_t size)
{
    static_cast<std::string*>(context)->append(bytes, size);
    return true;
}

bool emit(const app::AppConfig& config, std::string& out)
{
    app::tms::LineScratch scratch{};
    out.clear();
    return app::tms::writeDocument(config, app::tms::DocumentKind::Working,
                                   {&out, append}, scratch
#if defined(TMS_COMPLETE_INTEROP)
                                   ,
                                   nullptr, app::sd_tms::settings_extension::writeRecords,
                                   const_cast<app::AppConfig*>(&config)
#endif
    );
}

bool decode(const std::string& text, app::AppConfig* target)
{
    if (text.find('\0') != std::string::npos) return false;
#if defined(TMS_COMPLETE_INTEROP)
    app::sd_tms::settings_extension::beginRead(target != nullptr);
    struct EndRead
    {
        ~EndRead() { app::sd_tms::settings_extension::endRead(); }
    } end_read;
    app::tms::Decoder decoder(target, app::tms::DocumentKind::Working,
                              app::sd_tms::settings_extension::consumeRecord, nullptr,
                              app::sd_tms::settings_extension::finishDocument);
#else
    app::tms::Decoder decoder(target, app::tms::DocumentKind::Working);
#endif
    std::size_t offset = 0;
    char line[app::tms::kMaxLineBytes]{};
    while (offset < text.size())
    {
        const auto end = text.find('\n', offset);
        if (end == std::string::npos || end - offset >= sizeof(line)) return false;
        std::memcpy(line, text.data() + offset, end - offset);
        line[end - offset] = 0;
        if (!decoder.consumeLine(line)) return false;
        offset = end + 1;
    }
    return decoder.finish();
}

uint32_t length(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8U |
           uint32_t(bytes[2]) << 16U | uint32_t(bytes[3]) << 24U;
}

bool reply(uint8_t status, const std::string& text)
{
    const auto size = static_cast<uint32_t>(text.size());
    const uint8_t header[] = {status, static_cast<uint8_t>(size),
                              static_cast<uint8_t>(size >> 8U),
                              static_cast<uint8_t>(size >> 16U),
                              static_cast<uint8_t>(size >> 24U)};
    return std::fwrite(header, 1, sizeof(header), stdout) == sizeof(header) &&
           std::fwrite(text.data(), 1, text.size(), stdout) == text.size();
}
} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc != 2) return 2;
#if defined(TMS_COMPLETE_INTEROP)
    resetCompleteConfigFixture();
#endif
    if (std::strcmp(argv[1], "--emit") == 0)
    {
        app::AppConfig source;
        std::string text;
        if (!emit(source, text)) return 3;
        return std::fwrite(text.data(), 1, text.size(), stdout) == text.size() ? 0 : 4;
    }
    if (std::strcmp(argv[1], "--batch") != 0) return 2;
    for (;;)
    {
        uint8_t header[4];
        const auto count = std::fread(header, 1, sizeof(header), stdin);
        if (count == 0 && std::feof(stdin)) return 0;
        if (count != sizeof(header)) return 5;
        const uint32_t size = length(header);
        if (size == 0 || size > kMaxDocument) return 6;
        std::string input(size, '\0'), output;
        if (std::fread(input.data(), 1, size, stdin) != size) return 7;
#if defined(TMS_COMPLETE_INTEROP)
        resetCompleteConfigFixture();
#endif
        app::AppConfig restored;
        if (!decode(input, nullptr))
        {
            if (!reply(1, output)) return 4;
        }
        else if (!decode(input, &restored))
        {
            if (!reply(2, output)) return 4;
        }
        else
        {
            if (!emit(restored, output) || !reply(0, output)) return 3;
        }
    }
}
