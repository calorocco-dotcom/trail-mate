#pragma once
#include <string_view>
namespace track_sharing
{
// Bounded sharing profile: XSD dateTime calendar/timezone syntax, seconds
// 00..59 (no leap-second conversion); no timezone means unspecified, not UTC.
inline bool validTrackDateTime(std::string_view text, bool allow_unknown = false)
{
    const auto whitespace = [](char ch)
    { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (!text.empty() && whitespace(text.front())) text.remove_prefix(1);
    while (!text.empty() && whitespace(text.back())) text.remove_suffix(1);
    if (text.empty()) return allow_unknown;
    if (text.size() > 63) return false;
    size_t i = 0;
    bool negative = false;
    if (text[i] == '-')
    {
        negative = true;
        ++i;
    }
    const auto start = i;
    unsigned year = 0;
    bool nonzero = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
    {
        const unsigned digit = text[i++] - '0';
        year = (year * 10 + digit) % 400;
        nonzero |= digit != 0;
    }
    if (i - start < 4 || !nonzero || (i - start > 4 && text[start] == '0')) return false;
    const auto symbol = [&](char ch)
    {if(i>=text.size()||text[i]!=ch)return false;++i;return true; };
    const auto two = [&](unsigned& value)
    {if(text.size()-i<2||text[i]<'0'||text[i]>'9'||text[i+1]<'0'||text[i+1]>'9')return false;value=(text[i]-'0')*10+text[i+1]-'0';i+=2;return true; };
    unsigned month, day, hour, minute, second;
    if (!symbol('-') || !two(month) || !symbol('-') || !two(day) || !symbol('T') || !two(hour) || !symbol(':') || !two(minute) || !symbol(':') || !two(second) || month < 1 || month > 12 || hour > 24 || minute > 59 || second > 59) return false;
    const unsigned calendar_year = negative ? (401 - year) % 400 : year;
    const bool leap = calendar_year % 4 == 0 && (calendar_year % 100 != 0 || calendar_year % 400 == 0);
    const unsigned days = month == 2 ? (leap ? 29 : 28) : (month == 4 || month == 6 || month == 9 || month == 11) ? 30
                                                                                                                  : 31;
    if (!day || day > days) return false;
    bool fraction_nonzero = false;
    if (i < text.size() && text[i] == '.')
    {
        const auto first = ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') fraction_nonzero |= text[i++] != '0';
        if (i == first) return false;
    }
    if (hour == 24 && (minute || second || fraction_nonzero)) return false;
    if (i == text.size()) return true;
    if (text[i] == 'Z') return i + 1 == text.size();
    if (text[i] != '+' && text[i] != '-') return false;
    ++i;
    unsigned zone_hour, zone_minute;
    return two(zone_hour) && symbol(':') && two(zone_minute) && i == text.size() && zone_hour <= 14 && zone_minute <= 59 && (zone_hour != 14 || !zone_minute);
}
} // namespace track_sharing
