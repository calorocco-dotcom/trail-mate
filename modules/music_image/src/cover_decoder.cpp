#include "music/cover_decoder.h"
#include "lvgl.h"
#include "platform/ui/psram_ui_lease.h"
#include "src/draw/lv_draw_buf_private.h"
#include "src/libs/tjpgd/tjpgd.h"
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"
#include <algorithm>
#include <cstring>
namespace music
{
namespace
{
constexpr uint32_t kMaxPictureBytes = 2U * 1024U * 1024U;
struct JpegWorkspace
{
    Reader* reader = nullptr;
    uint32_t start = 0, length = 0, cursor = 0;
    Artwork* art = nullptr;
    bool (*pump)(void*) = nullptr;
    void* context = nullptr;
    unsigned blocks = 0;
    JDEC decoder{};
    alignas(8) uint8_t pool[8192]{};
};
std::size_t jpeg_read(JDEC* decoder, uint8_t* output, std::size_t requested)
{
    auto& self = *static_cast<JpegWorkspace*>(decoder->device);
    const auto size = std::min<std::size_t>(requested, self.length - self.cursor);
    const auto got = output ? self.reader->read_at(self.start + self.cursor, output, size) : size;
    self.cursor += got;
    return got;
}
uint16_t rgb565(unsigned r, unsigned g, unsigned b) { return static_cast<uint16_t>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
int jpeg_output(JDEC* decoder, void* bitmap, JRECT* rect)
{
    auto& self = *static_cast<JpegWorkspace*>(decoder->device);
    auto& art = *self.art;
    if (self.pump && ++self.blocks % 16 == 0 && !self.pump(self.context)) return 0;
    // LVGL's TJpgDec fork emits BGR888 for JD_FORMAT=0 (unlike upstream).
    // Convert to native RGB565 here; display byte swapping belongs to flush.
    static_assert(JD_FORMAT == 0, "Music JPEG adapter requires LVGL BGR888 output");
    const auto* bgr = static_cast<const uint8_t*>(bitmap);
    const uint32_t block_w = rect->right - rect->left + 1;
    // Map destination centres into this MCU. Original aspect ratio is retained.
    const uint32_t first_y = static_cast<uint32_t>(rect->top) * art.height / decoder->height;
    const uint32_t last_y = std::min<uint32_t>(art.height, static_cast<uint32_t>(rect->bottom + 1) * art.height / decoder->height + 1);
    const uint32_t first_x = static_cast<uint32_t>(rect->left) * art.width / decoder->width;
    const uint32_t last_x = std::min<uint32_t>(art.width, static_cast<uint32_t>(rect->right + 1) * art.width / decoder->width + 1);
    for (uint32_t y = first_y; y < last_y; y++)
    {
        const uint32_t sy = (2 * y + 1) * decoder->height / (2 * art.height);
        if (sy < rect->top || sy > rect->bottom) continue;
        for (uint32_t x = first_x; x < last_x; x++)
        {
            const uint32_t sx = (2 * x + 1) * decoder->width / (2 * art.width);
            if (sx < rect->left || sx > rect->right) continue;
            const auto at = ((sy - rect->top) * block_w + sx - rect->left) * 3;
            art.pixels[y * art.width + x] = rgb565(bgr[at + 2], bgr[at + 1], bgr[at]);
        }
    }
    return 1;
}
Artwork* jpeg(Reader& input, const Track& track, uint16_t edge, CoverResult& result, bool (*pump)(void*), void* context)
{
    auto* workspace = make<JpegWorkspace>();
    if (!workspace)
    {
        result = CoverResult::OutOfMemory;
        return nullptr;
    }
    workspace->reader = &input;
    workspace->pump = pump;
    workspace->context = context;
    workspace->start = track.picture_offset;
    workspace->length = track.picture_size;
    const auto prepared = jd_prepare(&workspace->decoder, jpeg_read, workspace->pool, sizeof(workspace->pool), workspace);
    if (prepared != JDR_OK)
    {
        result = prepared == JDR_FMT3 ? CoverResult::Unsupported : CoverResult::Invalid;
        destroy(workspace);
        return nullptr;
    }
    const auto width = workspace->decoder.width, height = workspace->decoder.height;
    if (!width || !height || width > 8192 || height > 8192)
    {
        result = CoverResult::TooLarge;
        destroy(workspace);
        return nullptr;
    }
    const auto longest = std::max(width, height);
    const auto w = std::max<unsigned>(1, static_cast<unsigned>(width) * edge / longest), h = std::max<unsigned>(1, static_cast<unsigned>(height) * edge / longest);
    auto* art = make_artwork(track.id, w, h);
    workspace->art = art;
    if (!art)
    {
        result = CoverResult::OutOfMemory;
        destroy(workspace);
        return nullptr;
    }
    const auto decoded = jd_decomp(&workspace->decoder, jpeg_output, 0);
    destroy(workspace);
    if (decoded != JDR_OK)
    {
        art->drop();
        result = decoded == JDR_INTR ? CoverResult::Cancelled : CoverResult::Invalid;
        return nullptr;
    }
    result = CoverResult::Ready;
    return art;
}
Artwork* png(Reader& input, const Track& track, uint16_t edge, CoverResult& result)
{
#if LV_USE_LODEPNG
    // Existing LVGL decoder allocations use the strict PSRAM allocator while this lease is held,
    // even if the user closes the player before decoding finishes.
    platform::ui::PsramUiLease memory;
    uint8_t header[33];
    if (track.picture_size < sizeof(header) || input.read_at(track.picture_offset, header, sizeof(header)) != sizeof(header))
    {
        result = CoverResult::Invalid;
        return nullptr;
    }
    auto* state = make<LodePNGState>();
    if (!state)
    {
        result = CoverResult::OutOfMemory;
        return nullptr;
    }
    lodepng_state_init(state);
    unsigned width = 0, height = 0;
    const unsigned inspected = lodepng_inspect(&width, &height, state, header, sizeof(header));
    // LodePNG expands full scanlines: cap that transient raster independently of JPEG dimensions.
    if (inspected || !width || !height || width > 512 || height > 512 || uint64_t(width) * height > 147456 || track.picture_size > 256U * 1024U)
    {
        lodepng_state_cleanup(state);
        destroy(state);
        result = inspected ? CoverResult::Invalid : CoverResult::TooLarge;
        return nullptr;
    }
    auto* encoded = static_cast<uint8_t*>(allocate(track.picture_size));
    if (!encoded)
    {
        lodepng_state_cleanup(state);
        destroy(state);
        result = CoverResult::OutOfMemory;
        return nullptr;
    }
    if (input.read_at(track.picture_offset, encoded, track.picture_size) != track.picture_size)
    {
        release(encoded);
        lodepng_state_cleanup(state);
        destroy(state);
        result = CoverResult::Invalid;
        return nullptr;
    }
    state->info_raw.colortype = LCT_RGBA;
    state->info_raw.bitdepth = 8;
    state->decoder.zlibsettings.max_output_size = (width * 4 + 8) * height + 1024;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    state->decoder.read_text_chunks = 0;
    state->decoder.remember_unknown_chunks = 0;
    state->decoder.max_icc_size = 4096;
    state->decoder.max_text_size = 4096;
#endif
    unsigned char* rgba = nullptr;
    const auto decoded = lodepng_decode(&rgba, &width, &height, state, encoded, track.picture_size);
    release(encoded);
    lodepng_state_cleanup(state);
    destroy(state);
    // LVGL's fork of LodePNG returns a draw-buffer object, unlike upstream's byte array.
    auto* decoded_buffer = reinterpret_cast<lv_draw_buf_t*>(rgba);
    if (decoded)
    {
        if (decoded_buffer) lv_draw_buf_destroy(decoded_buffer);
        result = decoded == 83 ? CoverResult::OutOfMemory : CoverResult::Invalid;
        return nullptr;
    }
    const auto* pixels = decoded_buffer->data;
    const auto longest = std::max(width, height);
    auto* art = make_artwork(track.id, std::max(1U, width * edge / longest), std::max(1U, height * edge / longest));
    if (art)
        for (unsigned y = 0; y < art->height; y++)
            for (unsigned x = 0; x < art->width; x++)
            {
                const auto at = (2 * y + 1) * height / (2 * art->height) * decoded_buffer->header.stride + (2 * x + 1) * width / (2 * art->width) * 4;
                const unsigned a = pixels[at + 3];
                art->pixels[y * art->width + x] = rgb565((pixels[at] * a + 250 * (255 - a)) / 255, (pixels[at + 1] * a + 240 * (255 - a)) / 255, (pixels[at + 2] * a + 216 * (255 - a)) / 255);
            }
    lv_draw_buf_destroy(decoded_buffer);
    result = art ? CoverResult::Ready : CoverResult::OutOfMemory;
    return art;
#else
    (void)input;
    (void)track;
    (void)edge;
    result = CoverResult::Unsupported;
    return nullptr;
#endif
}
} // namespace
Artwork* decode_cover(Reader& input, const Track& track, uint16_t edge, CoverResult& result, bool (*pump)(void*), void* context)
{
    if (!track.picture_size)
    {
        result = CoverResult::Missing;
        return nullptr;
    }
    if (track.picture_size > kMaxPictureBytes)
    {
        result = CoverResult::TooLarge;
        return nullptr;
    }
    if (track.picture_offset > input.size() || track.picture_size > input.size() - track.picture_offset)
    {
        result = CoverResult::Invalid;
        return nullptr;
    }
    edge = std::clamp<uint16_t>(edge, 32, 240);
    if (pump && !pump(context))
    {
        result = CoverResult::Cancelled;
        return nullptr;
    }
    if (track.picture_format == PictureFormat::Jpeg) return jpeg(input, track, edge, result, pump, context);
    if (track.picture_format == PictureFormat::Png) return png(input, track, edge, result);
    result = CoverResult::Unsupported;
    return nullptr;
}
} // namespace music
