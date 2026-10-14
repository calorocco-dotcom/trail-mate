#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace track_sharing
{
// UI coordinate events only: no point list and no change to signed geometry
// admission. KML comma tuples and gx whitespace triples have distinct grammar.
class KmlUiCoordinateStream
{
  public:
    using Callback = bool (*)(void*, double, double, double, bool);
    KmlUiCoordinateStream(bool gx, void* context, Callback callback) : context_(context), callback_(callback), gx_(gx) {}
    bool feed(const char* text, size_t length)
    {
        if (failed_ || finished_ || (!text && length)) return false;
        for (size_t i = 0; i < length; ++i)
        {
            const char c = text[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (size_ && !number()) return fail();
                if (!gx_ && parts_ && !point()) return fail();
            }
            else if (c == ',' && !gx_)
            {
                if (!size_ || !number() || parts_ >= 3) return fail();
                comma_ = true;
            }
            else
            {
                if (size_ >= 63 || !((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) return fail();
                token_[size_++] = c;
                comma_ = false;
            }
        }
        return true;
    }
    bool finish()
    {
        if (failed_ || finished_ || comma_ || (size_ && !number()) || (parts_ && !point()) || (!emitted_ && !gx_)) return fail();
        finished_ = true;
        return true; // an empty gx:coord is an explicitly missing sample
    }

  private:
    bool fail()
    {
        failed_ = true;
        return false;
    }
    bool number()
    {
        if (parts_ >= 3) return false;
        size_t i = 0;
        if (token_[i] == '+' || token_[i] == '-') ++i;
        unsigned digits = 0;
        while (i < size_ && token_[i] >= '0' && token_[i] <= '9') ++i, ++digits;
        if (i < size_ && token_[i] == '.')
        {
            ++i;
            while (i < size_ && token_[i] >= '0' && token_[i] <= '9') ++i, ++digits;
        }
        if (!digits) return false;
        if (i < size_ && (token_[i] == 'e' || token_[i] == 'E'))
        {
            ++i;
            if (i < size_ && (token_[i] == '+' || token_[i] == '-')) ++i;
            const auto start = i;
            while (i < size_ && token_[i] >= '0' && token_[i] <= '9') ++i;
            if (i == start) return false;
        }
        if (i != size_) return false;
        token_[size_] = '\0';
        const double value = std::strtod(token_, nullptr);
        if (!std::isfinite(value)) return false;
        values_[parts_++] = value;
        size_ = 0;
        return true;
    }
    bool point()
    {
        if (comma_ || parts_ < 2 || parts_ > 3 || (gx_ && parts_ != 3) || std::abs(values_[0]) > 180 || std::abs(values_[1]) > 90 || emitted_ >= 100000) return false;
        if (callback_ && !callback_(context_, values_[1], values_[0], parts_ == 3 ? values_[2] : 0, parts_ == 3)) return false;
        ++emitted_;
        parts_ = 0;
        return true;
    }
    void* context_;
    Callback callback_;
    double values_[3]{};
    char token_[64]{};
    uint32_t emitted_ = 0;
    uint8_t size_ = 0, parts_ = 0;
    bool gx_ = false, comma_ = false, failed_ = false, finished_ = false;
};
static_assert(sizeof(KmlUiCoordinateStream) <= 128, "UI coordinates retain only one tuple");
} // namespace track_sharing
