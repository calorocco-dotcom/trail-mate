#pragma once
#include <cstdint>
#include <string_view>
namespace track_sharing
{
inline bool xmlNameStart(uint32_t cp, bool nc)
{
    return (!nc && cp == ':') || cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= 0xc0 && cp <= 0xd6) || (cp >= 0xd8 && cp <= 0xf6) || (cp >= 0xf8 && cp <= 0x2ff) ||
           (cp >= 0x370 && cp <= 0x37d) || (cp >= 0x37f && cp <= 0x1fff) || (cp >= 0x200c && cp <= 0x200d) ||
           (cp >= 0x2070 && cp <= 0x218f) || (cp >= 0x2c00 && cp <= 0x2fef) || (cp >= 0x3001 && cp <= 0xd7ff) ||
           (cp >= 0xf900 && cp <= 0xfdcf) || (cp >= 0xfdf0 && cp <= 0xfffd) || (cp >= 0x10000 && cp <= 0xeffff);
}
inline bool validXmlName(std::string_view name, bool nc = true)
{
    if (name.empty()) return false;
    size_t i = 0;
    bool first = true;
    while (i < name.size())
    {
        uint32_t cp = static_cast<uint8_t>(name[i++]), minimum = 0;
        unsigned remaining = 0;
        if (cp >= 0xc2 && cp <= 0xdf)
        {
            cp &= 31;
            remaining = 1;
            minimum = 0x80;
        }
        else if (cp >= 0xe0 && cp <= 0xef)
        {
            cp &= 15;
            remaining = 2;
            minimum = 0x800;
        }
        else if (cp >= 0xf0 && cp <= 0xf4)
        {
            cp &= 7;
            remaining = 3;
            minimum = 0x10000;
        }
        else if (cp >= 128) return false;
        if (remaining > name.size() - i) return false;
        while (remaining--)
        {
            const auto byte = static_cast<uint8_t>(name[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (byte & 63);
        }
        if (cp < minimum) return false;
        const bool start = xmlNameStart(cp, nc), extra = cp == '-' || cp == '.' || (cp >= '0' && cp <= '9') || cp == 0xb7 || (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x203f && cp <= 0x2040);
        if (!start && (first || !extra)) return false;
        first = false;
    }
    return true;
}
inline bool validXmlQName(std::string_view name)
{
    const auto colon = name.find(':');
    return colon == name.npos ? validXmlName(name) : name.find(':', colon + 1) == name.npos && validXmlName(name.substr(0, colon)) && validXmlName(name.substr(colon + 1));
}
} // namespace track_sharing
