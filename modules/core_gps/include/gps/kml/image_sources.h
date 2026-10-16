#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gps::kml
{
namespace image_source_detail
{
constexpr bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
constexpr char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
inline bool equal(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != b[i]) return false;
    return true;
}
} // namespace image_source_detail

// One pass: decoded '&' never begins another entity. Supported named entities
// are the XML/HTML attribute basics; other names remain literal. Numeric forms
// follow HTML replacement rules. This does not change the source KML bytes.
inline std::string decodeImageSource(std::string_view source)
{
    std::string output;
    output.reserve(source.size());
    for (size_t i = 0; i < source.size();)
    {
        uint32_t cp = 0;
        const auto tail_end = source[i] == '&' ? source.substr(i + 1, 16).find(';') : source.npos;
        const auto end = tail_end == source.npos ? source.npos : i + 1 + tail_end;
        bool decoded = false;
        if (end != source.npos && end - i <= 16)
        {
            auto entity = source.substr(i + 1, end - i - 1);
            if (entity == "amp") cp = '&', decoded = true;
            else if (entity == "quot") cp = '"', decoded = true;
            else if (entity == "apos") cp = '\'', decoded = true;
            else if (entity == "lt") cp = '<', decoded = true;
            else if (entity == "gt") cp = '>', decoded = true;
            else if (!entity.empty() && entity.front() == '#')
            {
                entity.remove_prefix(1);
                unsigned base = 10;
                if (!entity.empty() && (entity.front() == 'x' || entity.front() == 'X')) base = 16, entity.remove_prefix(1);
                decoded = !entity.empty();
                bool oversized = false;
                for (const auto c : entity)
                {
                    const auto lower = image_source_detail::lower(c);
                    const unsigned digit = c >= '0' && c <= '9' ? unsigned(c - '0') : lower >= 'a' && lower <= 'f' ? unsigned(lower - 'a' + 10)
                                                                                                                   : base;
                    if (digit >= base)
                    {
                        decoded = false;
                        break;
                    }
                    if (cp > (0x10ffffU - digit) / base) oversized = true;
                    else if (!oversized) cp = cp * base + digit;
                }
                if (decoded)
                {
                    if (oversized || !cp || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
                    if (cp >= 0x80 && cp <= 0x9f)
                    {
                        static constexpr uint16_t c1[] = {0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d, 0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178};
                        cp = c1[cp - 0x80];
                    }
                }
            }
        }
        if (!decoded)
        {
            output.push_back(source[i++]);
            continue;
        }
        i = end + 1;
        if (cp <= 0x7f) output.push_back(char(cp));
        else if (cp <= 0x7ff)
        {
            output.push_back(char(0xc0 | (cp >> 6)));
            output.push_back(char(0x80 | (cp & 63)));
        }
        else if (cp <= 0xffff)
        {
            output.push_back(char(0xe0 | (cp >> 12)));
            output.push_back(char(0x80 | ((cp >> 6) & 63)));
            output.push_back(char(0x80 | (cp & 63)));
        }
        else
        {
            output.push_back(char(0xf0 | (cp >> 18)));
            output.push_back(char(0x80 | ((cp >> 12) & 63)));
            output.push_back(char(0x80 | ((cp >> 6) & 63)));
            output.push_back(char(0x80 | (cp & 63)));
        }
    }
    return output;
}

// Extract literal HTML img/src attributes, without allocating a tag copy.
// Callback receives a borrowed, still entity-encoded view and returns whether
// to continue. This is NOT a streaming KML parser or a full HTML DOM parser:
// incomplete tags are ignored, and entity/URL policy belongs to the caller.
template <class Callback>
void forEachImageSource(std::string_view text, Callback&& callback)
{
    using namespace image_source_detail;
    size_t offset = 0;
    std::string_view raw_text;
    while (offset < text.size())
    {
        const auto opening = text.find('<', offset);
        if (opening == text.npos) return;
        offset = opening + 1;
        if (!raw_text.empty())
        {
            if (offset < text.size() && text[offset] == '/' &&
                offset + 1 + raw_text.size() <= text.size() &&
                equal(text.substr(offset + 1, raw_text.size()), raw_text))
            {
                const auto after = offset + 1 + raw_text.size();
                if (after < text.size() && (space(text[after]) || text[after] == '>')) raw_text = {};
            }
            if (!raw_text.empty()) continue;
        }
        if (text.substr(opening, 4) == "<!--")
        {
            const auto end = text.find("-->", opening + 4);
            if (end == text.npos) return;
            offset = end + 3;
            continue;
        }
        // Callers currently pass complete KML lines, including CDATA wrappers.
        if (text.substr(opening, 9) == "<![CDATA[")
        {
            offset = opening + 9;
            continue;
        }
        if (offset >= text.size()) return;
        const bool closing = text[offset] == '/';
        if (closing) ++offset;
        const auto name_start = offset;
        while (offset < text.size() && !space(text[offset]) && text[offset] != '/' && text[offset] != '>') ++offset;
        const auto name = text.substr(name_start, offset - name_start);
        const bool image = !closing && equal(name, "img");
        std::string_view source;
        bool source_seen = false, complete = false, invalid = false;
        while (offset < text.size())
        {
            while (offset < text.size() && (space(text[offset]) || text[offset] == '/')) ++offset;
            if (offset >= text.size()) break;
            if (text[offset] == '>')
            {
                ++offset;
                complete = true;
                break;
            }
            const auto attribute_start = offset;
            while (offset < text.size() && !space(text[offset]) && text[offset] != '=' && text[offset] != '/' && text[offset] != '>')
            {
                if (text[offset] == '<' || text[offset] == '\'' || text[offset] == '"') invalid = true;
                ++offset;
            }
            const auto attribute = text.substr(attribute_start, offset - attribute_start);
            while (offset < text.size() && space(text[offset])) ++offset;
            const bool take = image && !source_seen && equal(attribute, "src");
            if (take) source_seen = true; // HTML keeps the first duplicate, even empty.
            if (offset >= text.size()) break;
            if (text[offset] != '=') continue;
            ++offset;
            while (offset < text.size() && space(text[offset])) ++offset;
            if (offset >= text.size()) break;
            const char quote = text[offset] == '\'' || text[offset] == '"' ? text[offset++] : '\0';
            const auto value_start = offset;
            if (quote)
            {
                const auto end = text.find(quote, offset);
                if (end == text.npos) return;
                offset = end;
            }
            else
            {
                while (offset < text.size() && !space(text[offset]) && text[offset] != '>') ++offset;
            }
            if (take) source = text.substr(value_start, offset - value_start);
            if (quote) ++offset;
        }
        if (!complete) return;
        if (image && !invalid && !source.empty() && !callback(source)) return;
        if (!closing && (equal(name, "script") || equal(name, "style") || equal(name, "textarea") || equal(name, "title"))) raw_text = name;
    }
}
} // namespace gps::kml
