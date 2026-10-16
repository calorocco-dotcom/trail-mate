#pragma once

#include "gps/kml/image_sources.h"
#include <cstring>

namespace gps::kml
{
// A bounded HTML attribute scanner for XML-decoded description text. The caller
// lends source storage; unrelated attribute values and descriptions are never
// retained. Callbacks receive the still HTML-entity-encoded source only after
// the entire tag closes. This is not an HTML renderer or a URL trust policy.
class HtmlImageSourceStream
{
  public:
    HtmlImageSourceStream(char* source, size_t capacity) : source_(source), capacity_(capacity) {}
    void reset()
    {
        state_ = State::Text;
        raw_[0] = '\0';
        stopped_ = false;
        comment_dashes_ = 0;
    }
    template <class Callback>
    void feed(std::string_view text, Callback&& callback)
    {
        using namespace image_source_detail;
        for (const char c : text)
        {
            if (stopped_) return;
            switch (state_)
            {
            case State::Text:
                if (c == '<') startTag();
                break;
            case State::Open:
                if (raw_[0] && c != '/')
                {
                    state_ = State::Text; // raw text only recognizes a close
                    if (c == '<') startTag();
                }
                else if (c == '/') closing_ = true, state_ = State::Name;
                else if (c == '!') state_ = State::Bang;
                else if (c == '?') state_ = State::Skip;
                else if ((lower(c) >= 'a' && lower(c) <= 'z'))
                {
                    state_ = State::Name;
                    appendName(c);
                }
                else state_ = State::Text;
                break;
            case State::Bang:
                state_ = c == '-' ? State::BangDash : State::Skip;
                if (c == '>') state_ = State::Text;
                break;
            case State::BangDash:
                state_ = c == '-' ? State::Comment : State::Skip;
                if (c == '>') state_ = State::Text;
                break;
            case State::Comment:
                if (c == '>' && comment_dashes_ >= 2) state_ = State::Text, comment_dashes_ = 0;
                else if (c == '-') comment_dashes_ = comment_dashes_ < 2 ? comment_dashes_ + 1 : 2;
                else comment_dashes_ = 0;
                break;
            case State::Skip:
                if (quote_)
                {
                    if (c == quote_) quote_ = 0;
                }
                else if (c == '\'' || c == '"') quote_ = c;
                else if (c == '>') state_ = State::Text;
                break;
            case State::Name:
                if (space(c) || c == '/' || c == '>')
                {
                    image_ = !raw_[0] && !closing_ && equal({name_, name_size_}, "img");
                    state_ = State::Attributes;
                    if (c == '>') finishTag(callback);
                }
                else appendName(c);
                break;
            case State::Attributes:
                if (c == '>') finishTag(callback);
                else if (!space(c) && c != '/')
                {
                    attribute_size_ = 0;
                    state_ = State::AttributeName;
                    appendAttribute(c);
                }
                break;
            case State::AttributeName:
                if (space(c) || c == '=' || c == '/' || c == '>')
                {
                    selectAttribute();
                    state_ = c == '=' ? State::BeforeValue : State::AfterAttribute;
                    if (c == '>') finishTag(callback);
                    else if (c == '/') state_ = State::Attributes;
                }
                else appendAttribute(c);
                break;
            case State::AfterAttribute:
                if (c == '=') state_ = State::BeforeValue;
                else if (c == '>') finishTag(callback);
                else if (!space(c))
                {
                    state_ = State::Attributes;
                    if (c != '/')
                    {
                        attribute_size_ = 0;
                        state_ = State::AttributeName;
                        appendAttribute(c);
                    }
                }
                break;
            case State::BeforeValue:
                if (space(c)) break;
                if (c == '\'' || c == '"') quote_ = c, state_ = State::QuotedValue;
                else if (c == '>') finishTag(callback);
                else state_ = State::Value, appendValue(c);
                break;
            case State::QuotedValue:
                if (c == quote_) quote_ = 0, state_ = State::Attributes;
                else appendValue(c);
                break;
            case State::Value:
                if (space(c)) state_ = State::Attributes;
                else if (c == '>') finishTag(callback);
                else appendValue(c);
                break;
            }
        }
    }

  private:
    enum class State : uint8_t
    {
        Text,
        Open,
        Bang,
        BangDash,
        Comment,
        Skip,
        Name,
        Attributes,
        AttributeName,
        AfterAttribute,
        BeforeValue,
        QuotedValue,
        Value
    };
    void startTag()
    {
        state_ = State::Open;
        name_size_ = attribute_size_ = source_size_ = 0;
        name_[0] = '\0';
        quote_ = 0;
        closing_ = image_ = source_seen_ = take_ = overflow_ = invalid_ = false;
    }
    void appendName(char c)
    {
        if (name_size_ < sizeof(name_) - 1) name_[name_size_++] = image_source_detail::lower(c), name_[name_size_] = '\0';
        else invalid_ = true;
    }
    void appendAttribute(char c)
    {
        if (c == '<' || c == '\'' || c == '"') invalid_ = true;
        if (attribute_size_ < sizeof(attribute_) - 1) attribute_[attribute_size_++] = image_source_detail::lower(c);
        else attribute_size_ = sizeof(attribute_) - 1; // cannot match a short src
    }
    void selectAttribute()
    {
        take_ = image_ && !source_seen_ && image_source_detail::equal({attribute_, attribute_size_}, "src");
        if (take_) source_seen_ = true;
    }
    void appendValue(char c)
    {
        if (!take_) return;
        if (source_ && source_size_ < capacity_) source_[source_size_++] = c;
        else overflow_ = true;
    }
    template <class Callback>
    void finishTag(Callback& callback)
    {
        using namespace image_source_detail;
        state_ = State::Text;
        if (raw_[0])
        {
            if (closing_ && !invalid_ && equal({name_, name_size_}, raw_)) raw_[0] = '\0';
            return;
        }
        if (image_ && !invalid_ && !overflow_ && source_size_ && !callback(std::string_view(source_, source_size_))) stopped_ = true;
        if (!closing_ && !invalid_ && (equal({name_, name_size_}, "script") || equal({name_, name_size_}, "style") || equal({name_, name_size_}, "textarea") || equal({name_, name_size_}, "title")))
            std::memcpy(raw_, name_, name_size_ + 1);
    }
    char* source_;
    size_t capacity_, source_size_ = 0;
    char name_[16]{}, attribute_[16]{}, raw_[16]{};
    uint8_t name_size_ = 0, attribute_size_ = 0, comment_dashes_ = 0;
    char quote_ = 0;
    State state_ = State::Text;
    bool closing_ = false, image_ = false, source_seen_ = false, take_ = false;
    bool overflow_ = false, invalid_ = false, stopped_ = false;
};
static_assert(sizeof(HtmlImageSourceStream) <= 128, "HTML scanner borrows the source buffer");
} // namespace gps::kml
