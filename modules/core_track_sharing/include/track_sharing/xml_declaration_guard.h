#pragma once
#include <cstdint>
#include <string_view>
namespace track_sharing
{
class XmlDeclarationGuard
{
  public:
    bool feed(uint8_t byte)
    {
        if (state_ == State::Failed) return false;
        if (state_ == State::Done) return true;
        if (state_ == State::Initial)
        {
            state_ = byte == 0xef ? State::BomSecond : State::Prefix;
            if (byte == 0xef) return true;
        }
        if (state_ == State::BomSecond)
        {
            if (byte != 0xbb) return fail();
            state_ = State::BomThird;
            return true;
        }
        if (state_ == State::BomThird)
        {
            if (byte != 0xbf) return fail();
            state_ = State::Prefix;
            return true;
        }
        if (state_ == State::Prefix)
        {
            constexpr char prefix[] = "<?xml";
            if (byte != prefix[position_])
            {
                state_ = State::Done;
                return true;
            }
            if (++position_ == 5) state_ = State::AfterPrefix;
            return true;
        }
        if (state_ == State::AfterPrefix)
        {
            if (!space(byte))
            {
                state_ = State::Done;
                return true;
            }
            state_ = State::Name;
            position_ = 0;
            return true;
        }
        if (state_ == State::Name)
        {
            if (!position_ && space(byte)) return true;
            if (!position_ && byte == '?')
            {
                if (!(seen_ & 1)) return fail();
                state_ = State::End;
                return true;
            }
            if (byte == '=' || space(byte))
            {
                if (!field()) return fail();
                state_ = byte == '=' ? State::Quote : State::Equals;
                return true;
            }
            if (byte < 'a' || byte > 'z' || position_ >= 10) return fail();
            name_[position_++] = char(byte);
            return true;
        }
        if (state_ == State::Equals)
        {
            if (space(byte)) return true;
            if (byte != '=') return fail();
            state_ = State::Quote;
            return true;
        }
        if (state_ == State::Quote)
        {
            if (space(byte)) return true;
            if (byte != '\'' && byte != '"') return fail();
            quote_ = byte;
            position_ = 0;
            state_ = State::Value;
            return true;
        }
        if (state_ == State::Value)
        {
            if (byte == quote_)
            {
                if (!value()) return fail();
                state_ = State::Separator;
                return true;
            }
            if (position_ >= 16 || byte >= 128) return fail();
            value_[position_++] = char(byte);
            return true;
        }
        if (state_ == State::Separator)
        {
            if (space(byte))
            {
                position_ = 0;
                state_ = State::Name;
                return true;
            }
            if (byte == '?')
            {
                state_ = State::End;
                return true;
            }
            return fail();
        }
        if (state_ == State::End)
        {
            if (byte != '>') return fail();
            state_ = State::Done;
            return true;
        }
        return fail();
    }
    bool complete() const { return state_ == State::Done; }

  private:
    enum class State : uint8_t
    {
        Initial,
        BomSecond,
        BomThird,
        Prefix,
        AfterPrefix,
        Name,
        Equals,
        Quote,
        Value,
        Separator,
        End,
        Done,
        Failed
    };
    static bool space(uint8_t byte) { return byte == 9 || byte == 10 || byte == 13 || byte == 32; }
    bool fail()
    {
        state_ = State::Failed;
        return false;
    }
    bool field()
    {
        const std::string_view name(name_, position_);
        field_ = name == "version" ? 1 : name == "encoding" ? 2
                                     : name == "standalone" ? 4
                                                            : 0;
        if (!field_ || (seen_ & field_) || (!seen_ && field_ != 1) || (seen_ & 4)) return false;
        seen_ |= field_;
        return true;
    }
    bool value()
    {
        const std::string_view text(value_, position_);
        if (field_ == 1) return text == "1.0";
        if (field_ == 4) return text == "yes" || text == "no";
        if (text.size() != 5) return false;
        constexpr char utf[] = "UTF-8";
        for (unsigned i = 0; i < 5; ++i)
        {
            const auto ch = text[i] >= 'a' && text[i] <= 'z' ? char(text[i] - 32) : text[i];
            if (ch != utf[i]) return false;
        }
        return true;
    }
    char name_[10]{}, value_[16]{};
    uint8_t position_ = 0, seen_ = 0, field_ = 0, quote_ = 0;
    State state_ = State::Initial;
};
static_assert(sizeof(XmlDeclarationGuard) <= 32, "XML declaration guard must not buffer a prolog");
} // namespace track_sharing
