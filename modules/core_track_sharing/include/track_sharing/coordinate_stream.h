#pragma once
#include "track_sharing/geometry_bounds.h"
#include <cmath>
#include <cstdlib>
namespace track_sharing
{
enum class CoordinateText : uint8_t
{
    KmlTuples,
    GxCoordinate
};
// XML layer must feed decoded text from the correct namespace/geometry only.
// Fixed numeric-token budget (63 ASCII bytes), one tuple; no point storage.
class CoordinateStream
{
  public:
    CoordinateStream(GeometryBounds& bounds, CoordinateText format) : bounds_(bounds), format_(format) {}
    bool feed(const char* text, size_t length)
    {
        if (failed_ || finished_ || (!text && length)) return false;
        for (size_t i = 0; i < length; ++i)
        {
            const char value = text[i];
            if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
            {
                if (size_ && !number()) return fail();
                if (format_ == CoordinateText::KmlTuples && parts_ && !point()) return fail();
            }
            else if (value == ',' && format_ == CoordinateText::KmlTuples)
            {
                if (!size_ || !number() || parts_ >= 3) return fail();
                comma_ = true;
            }
            else
            {
                if (size_ >= 63 || !((value >= '0' && value <= '9') || value == '+' || value == '-' || value == '.' || value == 'e' || value == 'E')) return fail();
                token_[size_++] = value;
                comma_ = false;
            }
        }
        return true;
    }
    bool finish(bool allow_empty_gx = false)
    {
        if (failed_ || finished_ || comma_ || (size_ && !number()) || (parts_ && !point()) || (!emitted_ && !(allow_empty_gx && format_ == CoordinateText::GxCoordinate))) return fail();
        finished_ = true;
        return true;
    }

    bool closedRing() const { return finished_ && !failed_ && emitted_ >= 4 && first_[0] == values_[0] && first_[1] == values_[1]; }
    char* idleScratch() { return !size_ && !parts_ ? token_ : nullptr; }

  private:
    bool fail()
    {
        failed_ = true;
        bounds_.add(INT64_MAX, 0);
        return false;
    }
    bool number()
    {
        if (parts_ >= 3) return false;
        size_t i = 0;
        if (token_[i] == '+' || token_[i] == '-') ++i;
        unsigned digits = 0;
        while (i < size_ && token_[i] >= '0' && token_[i] <= '9')
        {
            ++i;
            ++digits;
        }
        if (i < size_ && token_[i] == '.')
        {
            ++i;
            while (i < size_ && token_[i] >= '0' && token_[i] <= '9')
            {
                ++i;
                ++digits;
            }
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
        if (comma_ || parts_ < 2 || parts_ > 3 || (format_ == CoordinateText::GxCoordinate && parts_ != 3) || std::abs(values_[0]) > 180 || std::abs(values_[1]) > 90) return false;
        if (!bounds_.add(static_cast<int64_t>(std::floor(values_[0] * 1e7 + 0.5)), static_cast<int64_t>(std::floor(values_[1] * 1e7 + 0.5)))) return false;
        if (!emitted_)
        {
            first_[0] = values_[0];
            first_[1] = values_[1];
        }
        ++emitted_;
        parts_ = 0;
        return true;
    }
    GeometryBounds& bounds_;
    double values_[3]{};
    double first_[2]{};
    char token_[64]{};
    uint32_t emitted_ = 0;
    uint8_t size_ = 0, parts_ = 0;
    CoordinateText format_;
    bool comma_ = false, failed_ = false, finished_ = false;
};
static_assert(sizeof(CoordinateStream) <= 136, "Coordinate parser must not store a point list");
} // namespace track_sharing
