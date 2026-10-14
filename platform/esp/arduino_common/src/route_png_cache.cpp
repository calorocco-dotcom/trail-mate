#include "platform/esp/arduino_common/route_png_cache.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_heap_caps.h>
#endif
#if __has_include(<lgfx/utility/lgfx_miniz.h>)
#include "../../../../third_party/route_png/route_pngle.h"
#include <lgfx/utility/lgfx_miniz.h>

extern "C" void* trail_mate_png_alloc(size_t bytes)
{
    if (!bytes || bytes > 65552) return nullptr;
#if defined(ESP_PLATFORM)
    if (!heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) return nullptr;
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
}
extern "C" void trail_mate_png_free(void* memory)
{
#if defined(ESP_PLATFORM)
    heap_caps_free(memory);
#else
    std::free(memory);
#endif
}
namespace platform::esp::route_images
{
namespace
{
namespace sd = arduino_common::storage;
constexpr uint32_t kPngLimit = 8U * 1024U * 1024U;
constexpr uint32_t kIhdr = 0x49484452, kPlte = 0x504c5445, kTrns = 0x74524e53, kIdat = 0x49444154, kIend = 0x49454e44;
class PngCacheWork
{
  public:
    PngCacheWork(sd::SdRuntimeFile& input, uint8_t* output, uint16_t width, uint16_t height)
        : input_(input), pixels_(output), media_(sd::sd_media_session()), width_(width), height_(height) {}
    bool current() const { return media_ && media_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
    static uint32_t big(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
    static void yield()
    {
#if defined(ESP_PLATFORM)
        taskYIELD();
#endif
    }
    bool exact(uint8_t* output, size_t bytes)
    {
        size_t read = 0;
        while (read < bytes)
        {
            if (!current()) return false;
            const int count = input_.read(output + read, bytes - read);
            if (count <= 0 || size_t(count) > bytes - read) return false;
            read += size_t(count);
            yield();
        }
        return true;
    }
    bool validate()
    {
        // Complete CRC/order/length proof before the allocating decoder. No
        // ancillary compression or caller-controlled allocation sizes.
        if (!current() || input_.size() > kPngLimit || input_.size() < 45 || !input_.seek(0) || !exact(scratch_, 8)) return false;
        constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (std::memcmp(scratch_, signature, 8)) return false;
        bool header = false, palette = false, transparency = false, data = false, data_ended = false;
        uint64_t data_bytes = 0;
        unsigned palette_count = 0;
        for (unsigned chunk = 0; chunk < 65536; ++chunk)
        {
            if (!current() || input_.position() > input_.size() || input_.size() - input_.position() < 12 || !exact(scratch_, 8)) return false;
            const uint32_t size = big(scratch_), type = big(scratch_ + 4);
            if (size > input_.size() - input_.position() - 4 || (!header && type != kIhdr)) return false;
            for (unsigned i = 4; i < 8; ++i)
                if (!((scratch_[i] >= 'A' && scratch_[i] <= 'Z') || (scratch_[i] >= 'a' && scratch_[i] <= 'z'))) return false;
            if (scratch_[6] & 32) return false; // PNG reserved bit
            if (!(scratch_[4] & 32) && type != kIhdr && type != kPlte && type != kIdat && type != kIend) return false;
            if (type == kIhdr && (header || size != 13)) return false;
            if (type == kPlte && (palette || data || !size || size > 768 || size % 3 || color_ == 0 || color_ == 4)) return false;
            if (type == kTrns && (transparency || data || !size ||
                                  (color_ == 0 ? size != 2 : color_ == 2 ? size != 6
                                                         : color_ == 3   ? !palette || size > palette_count
                                                                         : true))) return false;
            if (type == kIdat && (data_ended || (color_ == 3 && !palette))) return false;
            if (type == kIend && (size || !data || !data_bytes)) return false;
            uint32_t crc = uint32_t(lgfx_mz_crc32(0, scratch_ + 4, 4));
            uint32_t remaining = size;
            while (remaining)
            {
                const auto count = std::min<size_t>(remaining, sizeof(scratch_));
                if (!exact(scratch_, count)) return false;
                crc = uint32_t(lgfx_mz_crc32(crc, scratch_, count));
                if (type == kIhdr)
                {
                    source_w_ = big(scratch_);
                    source_h_ = big(scratch_ + 4);
                    depth_ = scratch_[8];
                    color_ = scratch_[9];
                    const bool depth_valid = color_ == 0   ? (depth_ == 1 || depth_ == 2 || depth_ == 4 || depth_ == 8 || depth_ == 16)
                                             : color_ == 3 ? (depth_ == 1 || depth_ == 2 || depth_ == 4 || depth_ == 8)
                                                           : (color_ == 2 || color_ == 4 || color_ == 6) && (depth_ == 8 || depth_ == 16);
                    if (!source_w_ || !source_h_ || source_w_ > 8192 || source_h_ > 8192 || uint64_t(source_w_) * source_h_ > 32000000 ||
                        !depth_valid || scratch_[10] || scratch_[11] || scratch_[12] > 1) return false;
                }
                remaining -= uint32_t(count);
            }
            if (!exact(scratch_, 4) || big(scratch_) != crc) return false;
            if (type == kIhdr) header = true;
            else if (type == kPlte)
            {
                palette_count = size / 3;
                if (color_ == 3 && palette_count > (1U << depth_)) return false;
                palette = true;
            }
            else if (type == kTrns) transparency = true;
            else if (type == kIdat)
            {
                data = true;
                data_bytes += size;
            }
            else if (data) data_ended = true;
            if (type == kIend) return current() && input_.position() == input_.size() && input_.seek(0);
        }
        return false;
    }
    void crop()
    {
        const double source = double(source_w_) / source_h_, target = double(width_) / height_;
        if (source > target)
        {
            crop_h_ = source_h_;
            crop_w_ = crop_h_ * target;
            crop_x_ = (source_w_ - crop_w_) / 2;
            crop_y_ = 0;
        }
        else
        {
            crop_w_ = source_w_;
            crop_h_ = crop_w_ / target;
            crop_x_ = 0;
            crop_y_ = (source_h_ - crop_h_) / 2;
        }
    }
    bool dimensionsMatch(uint32_t width, uint32_t height) const { return width == source_w_ && height == source_h_; }
    bool drawable() const { return current() && !draw_error_; }
    static uint32_t read(void* context, uint8_t* buffer, uint32_t bytes)
    {
        auto& self = *static_cast<PngCacheWork*>(context);
        if (!self.current() || self.input_.position() > self.input_.size() || bytes > self.input_.size() - self.input_.position()) return 0;
        const bool ok = buffer ? self.exact(buffer, bytes) : self.input_.seek(self.input_.position() + bytes);
        yield();
        return ok ? bytes : 0;
    }
    static void draw(void* context, uint32_t x, uint32_t y, uint_fast8_t step, size_t count, const uint8_t* argb)
    {
        auto& s = *static_cast<PngCacheWork*>(context);
        if (!step || !count || !argb || y >= s.source_h_ || x >= s.source_w_ || count > (s.source_w_ - x + step - 1) / step)
        {
            s.draw_error_ = true;
            return;
        }
        const uint32_t last = x + uint32_t(count - 1) * step;
        const int dy0 = std::max(0, int(std::floor((double(y) - s.crop_y_) * s.height_ / s.crop_h_)) - 1);
        const int dy1 = std::min(int(s.height_) - 1, int(std::ceil((double(y + 1) - s.crop_y_) * s.height_ / s.crop_h_)) + 1);
        const int dx0 = std::max(0, int(std::floor((double(x) - s.crop_x_) * s.width_ / s.crop_w_)) - 1);
        const int dx1 = std::min(int(s.width_) - 1, int(std::ceil((double(last + 1) - s.crop_x_) * s.width_ / s.crop_w_)) + 1);
        for (int dy = dy0; dy <= dy1; ++dy)
        {
            const bool wide = uint64_t(s.source_w_) * s.height_ > uint64_t(s.source_h_) * s.width_;
            const uint32_t sy = wide ? uint64_t(2 * dy + 1) * s.source_h_ / (2 * s.height_)
                                     : (uint64_t(s.source_h_) * s.width_ - uint64_t(s.source_w_) * s.height_ + uint64_t(2 * dy + 1) * s.source_w_) / (2 * s.width_);
            if (sy != y) continue;
            for (int dx = dx0; dx <= dx1; ++dx)
            {
                const uint32_t sx = wide ? (uint64_t(s.source_w_) * s.height_ - uint64_t(s.source_h_) * s.width_ + uint64_t(2 * dx + 1) * s.source_h_) / (2 * s.height_)
                                         : uint64_t(2 * dx + 1) * s.source_w_ / (2 * s.width_);
                if (sx < x || sx > last || (sx - x) % step) continue;
                const auto* pixel = argb + ((sx - x) / step) * 4;
                auto* target = s.pixels_ + (size_t(dy) * s.width_ + dx) * 3;
                const unsigned alpha = pixel[0];
                for (unsigned channel = 0; channel < 3; ++channel)
                    target[channel] = uint8_t((unsigned(pixel[3 - channel]) * alpha + 255 * (255 - alpha) + 127) / 255);
            }
        }
        yield();
    }

  private:
    sd::SdRuntimeFile& input_;
    uint8_t* pixels_;
    uint32_t media_, source_w_ = 0, source_h_ = 0;
    uint16_t width_, height_;
    uint8_t depth_ = 0, color_ = 0, scratch_[512]{};
    bool draw_error_ = false;
    double crop_x_ = 0, crop_y_ = 0, crop_w_ = 0, crop_h_ = 0;
};
static_assert(sizeof(PngCacheWork) <= 768, "PNG transport/CRC/crop scratch is transient PSRAM");
} // namespace
bool decodeRoutePng(sd::SdRuntimeFile& input, uint8_t* bgr, size_t capacity, uint16_t width, uint16_t height)
{
    if (!bgr || !width || !height || width > 320 || height > 180 || capacity < size_t(width) * height * 3) return false;
    void* memory = trail_mate_png_alloc(sizeof(PngCacheWork));
    if (!memory) return false;
    auto* work = new (memory) PngCacheWork(input, bgr, width, height);
    bool ok = work->validate();
    pngle_t* decoder = ok ? trail_mate_pngle_new() : nullptr;
    ok = decoder && trail_mate_pngle_prepare(decoder, &PngCacheWork::read, work) == 0 && work->dimensionsMatch(trail_mate_pngle_get_width(decoder), trail_mate_pngle_get_height(decoder));
    if (ok)
    {
        work->crop();
        ok = trail_mate_pngle_decomp(decoder, &PngCacheWork::draw) == 0 && work->drawable() && input.position() == input.size();
    }
    trail_mate_pngle_destroy(decoder);
    work->~PngCacheWork();
    trail_mate_png_free(memory);
    return ok;
}
} // namespace platform::esp::route_images
#else
namespace platform::esp::route_images
{
bool decodeRoutePng(arduino_common::storage::SdRuntimeFile&, uint8_t*, size_t, uint16_t, uint16_t) { return false; }
} // namespace platform::esp::route_images
#endif
