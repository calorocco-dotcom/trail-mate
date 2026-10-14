#include "../../third_party/route_png/route_pngle.h"
#include "platform/esp/arduino_common/route_png_cache.h"
#include "platform/ui/route_storage.h"
#include "src/libs/tjpgd/tjpgd.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <lgfx/utility/lgfx_miniz.h>
#include <memory>
#include <string>
#include <vector>
static unsigned yields = 0, change_at = 0;
void route_png_test_yield()
{
    if (++yields == change_at) ++image_fake::media;
}
inline size_t heap_caps_get_free_size(unsigned) { return 1048576; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return 1048576; }
namespace sd = platform::esp::arduino_common::storage;
namespace platform::esp::arduino_common::storage
{
bool sd_exists(const char* path) { return image_fake::files.count(path) != 0; }
bool sd_remove(const char* path) { return sd_remove(path, image_fake::media); }
bool sd_rename(const char* from, const char* to) { return sd_rename(from, to, image_fake::media); }
} // namespace platform::esp::arduino_common::storage
namespace platform::ui::route_storage
{
constexpr size_t kRouteCacheInternalReserveBytes = 96 * 1024, kRouteCacheInternalSlackBytes = 8 * 1024, kRouteJpegDecoderWorkBytes = 3100;
constexpr uint16_t kRouteViewWidth = 320, kRouteViewHeight = 180;
bool route_asset_file_exists(const std::string& path) { return sd::sd_exists(path.c_str()); }
bool ensure_parent_dir(const std::string&) { return true; }
void log_cache_memory_skip(const char*, size_t) {}
// clang-format off
#include "image_ui_png_heap_actual.inc"
#include "image_ui_png_cache_actual.inc"
// clang-format on
} // namespace platform::ui::route_storage
using Bytes = std::vector<uint8_t>;
static void big(Bytes& output, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) output.push_back(uint8_t(value >> shift));
}
static uint32_t crc(const uint8_t* input, size_t size)
{
    uint32_t value = ~0u;
    for (size_t i = 0; i < size; ++i)
    {
        value ^= input[i];
        for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
    }
    return ~value;
}
static void chunk(Bytes& output, const char* type, const Bytes& data)
{
    big(output, uint32_t(data.size()));
    const auto start = output.size();
    output.insert(output.end(), type, type + 4);
    output.insert(output.end(), data.begin(), data.end());
    big(output, crc(output.data() + start, output.size() - start));
}
static Bytes zlib(const Bytes& raw)
{
    Bytes output{0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (const auto byte : raw)
    {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t at = 0; at < raw.size();)
    {
        const uint16_t count = uint16_t(std::min<size_t>(65535, raw.size() - at));
        output.push_back(at + count == raw.size() ? 1 : 0);
        output.push_back(uint8_t(count));
        output.push_back(uint8_t(count >> 8));
        output.push_back(uint8_t(~count));
        output.push_back(uint8_t((~count) >> 8));
        output.insert(output.end(), raw.begin() + at, raw.begin() + at + count);
        at += count;
    }
    big(output, (b << 16) | a);
    return output;
}
static int paeth(int a, int b, int c)
{
    const auto p = a + b - c;
    const int x = std::abs(p - a), y = std::abs(p - b), z = std::abs(p - c);
    return x <= y && x <= z ? a : y <= z ? b
                                         : c;
}
struct Fixture
{
    unsigned width, height;
    Bytes png, rgba;
};
// Independent PNG encoder: stored deflate, all five filters, packed samples,
// full-precision transparency, Adam7 passes and split/empty IDATs. Expected
// pixels are computed from sample values before encoding, not decoder output.
static Fixture fixture(unsigned w, unsigned h, unsigned color, unsigned depth, bool interlaced, bool transparency = false, unsigned raster_fault = 0)
{
    Fixture result{w, h, {137, 80, 78, 71, 13, 10, 26, 10}, {}};
    Bytes header;
    big(header, w);
    big(header, h);
    header.insert(header.end(), {uint8_t(depth), uint8_t(color), 0, 0, uint8_t(interlaced)});
    chunk(result.png, "IHDR", header);
    const unsigned channels = color == 0 || color == 3 ? 1 : color == 2 ? 3
                                                         : color == 4   ? 2
                                                                        : 4;
    const unsigned palettes = color == 3 ? std::min(16U, 1U << depth) : 0;
    const auto sample = [&](unsigned x, unsigned y, unsigned c)
    {
        if (color == 3) return (x + y) % palettes;
        if ((color == 4 && c == 1) || (color == 6 && c == 3)) return (x + y) % 3 == 0 ? 0U : (x + y) % 3 == 1 ? (depth == 16 ? 0x8080U : 128U)
                                                                                                              : (depth == 16 ? 65535U : 255U);
        return depth == 16 ? (c + 1) * 0x1000U + 0x34U + (x + y) % 2 : (x * 5 + y * 7 + c * 3) & ((1U << depth) - 1);
    };
    Bytes palette, alpha;
    for (unsigned i = 0; i < palettes; ++i)
    {
        palette.insert(palette.end(), {uint8_t(i * 37), uint8_t(i * 91), uint8_t(i * 13 + 77)});
        alpha.push_back(i % 3 == 0 ? 0 : i % 3 == 1 ? 128
                                                    : 255);
    }
    if (palettes) chunk(result.png, "PLTE", palette);
    if (transparency)
    {
        Bytes values;
        if (palettes) values = alpha;
        else
            for (unsigned c = 0; c < (color == 0 ? 1U : 3U); ++c)
            {
                const auto v = sample(0, 0, c);
                values.push_back(uint8_t(v >> 8));
                values.push_back(uint8_t(v));
            }
        chunk(result.png, "tRNS", values);
    }
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
        {
            unsigned r, g, b, a = 255;
            if (palettes)
            {
                const auto i = sample(x, y, 0);
                r = palette[i * 3];
                g = palette[i * 3 + 1];
                b = palette[i * 3 + 2];
                if (transparency) a = alpha[i];
            }
            else
            {
                const auto gray = color == 0 || color == 4;
                const auto value = [&](unsigned c)
                { const auto v = sample(x,y,c); return depth == 16 ? v >> 8 : depth < 8 ? v * 255 / ((1U << depth)-1) : v; };
                r = value(0);
                g = gray ? r : value(1);
                b = gray ? r : value(2);
                if (color == 4 || color == 6) a = value(channels - 1);
                if (transparency)
                {
                    bool match = true;
                    for (unsigned c = 0; c < (gray ? 1U : 3U); ++c) match &= sample(x, y, c) == sample(0, 0, c);
                    if (match) a = 0;
                }
            }
            result.rgba.insert(result.rgba.end(), {uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)});
        }
    constexpr unsigned x0[] = {0, 4, 0, 2, 0, 1, 0, 0}, y0[] = {0, 0, 4, 0, 2, 0, 1, 0};
    constexpr unsigned dx[] = {8, 8, 4, 4, 2, 2, 1, 1}, dy[] = {8, 8, 8, 4, 4, 2, 2, 1};
    Bytes raster;
    for (unsigned pass = interlaced ? 0 : 7; pass < 8; ++pass)
    {
        if (interlaced && pass == 7) break;
        Bytes previous;
        unsigned row_number = 0;
        for (unsigned y = y0[pass]; y < h; y += dy[pass])
        {
            Bytes row;
            unsigned bits = 0;
            for (unsigned x = x0[pass]; x < w; x += dx[pass])
                for (unsigned c = 0; c < channels; ++c)
                {
                    const auto v = sample(x, y, c);
                    if (depth == 16)
                    {
                        row.push_back(uint8_t(v >> 8));
                        row.push_back(uint8_t(v));
                    }
                    else if (depth == 8) row.push_back(uint8_t(v));
                    else
                    {
                        if (!(bits % 8)) row.push_back(0);
                        row.back() |= uint8_t(v << (8 - depth - bits % 8));
                        bits += depth;
                    }
                }
            if (row.empty()) continue;
            if (previous.empty()) previous.resize(row.size());
            const unsigned filter = (row_number++ + pass) % 5, bpp = std::max(1U, (channels * depth + 7) / 8);
            raster.push_back(uint8_t(filter));
            for (size_t i = 0; i < row.size(); ++i)
            {
                const int left = i < bpp ? 0 : row[i - bpp], up = previous[i], upper_left = i < bpp ? 0 : previous[i - bpp];
                const int predicted = filter == 0 ? 0 : filter == 1 ? left
                                                    : filter == 2   ? up
                                                    : filter == 3   ? (left + up) / 2
                                                                    : paeth(left, up, upper_left);
                raster.push_back(uint8_t(row[i] - predicted));
            }
            previous = std::move(row);
        }
    }
    if (raster_fault == 1) raster.push_back(0);
    if (raster_fault == 2) raster[0] = 5;
    if (raster_fault == 3) raster.pop_back();
    auto compressed = zlib(raster);
    if (raster_fault == 4) compressed.back() ^= 1;
    if (raster_fault == 5) compressed.push_back(0);
    chunk(result.png, "IDAT", {});
    for (size_t at = 0; at < compressed.size();)
    {
        const auto n = std::min<size_t>(137, compressed.size() - at);
        chunk(result.png, "IDAT", {compressed.begin() + at, compressed.begin() + at + n});
        at += n;
    }
    chunk(result.png, "IDAT", {});
    chunk(result.png, "IEND", {});
    return result;
}
static uint32_t little(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
static bool generate(const Bytes& input, unsigned w, unsigned h)
{
    image_fake::files.clear();
    image_fake::files["/source.jpg"] = input; // file suffix does not determine format
    return platform::ui::route_storage::generate_route_image_bmp_cache("/source.jpg", "/cache.bmp", uint16_t(w), uint16_t(h));
}
static void check(const Fixture& input, unsigned w, unsigned h)
{
    assert(generate(input.png, w, h));
    const auto& bmp = image_fake::files.at("/cache.bmp");
    assert(bmp[0] == 'B' && bmp[1] == 'M' && little(bmp.data() + 18) == w && little(bmp.data() + 22) == h);
    const auto stride = (w * 3 + 3) & ~3U;
    assert(bmp.size() == 54 + stride * h && !image_fake::files.count("/cache.bmp.tmp"));
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
        {
            unsigned sx, sy;
            if (input.width * h > input.height * w)
            {
                sx = (uint64_t(input.width) * h - uint64_t(input.height) * w + uint64_t(2 * x + 1) * input.height) / (2 * h);
                sy = uint64_t(2 * y + 1) * input.height / (2 * h);
            }
            else
            {
                sx = uint64_t(2 * x + 1) * input.width / (2 * w);
                sy = (uint64_t(input.height) * w - uint64_t(input.width) * h + uint64_t(2 * y + 1) * input.width) / (2 * w);
            }
            const auto* rgba = input.rgba.data() + (size_t(sy) * input.width + sx) * 4;
            const auto* pixel = bmp.data() + 54 + size_t(h - y - 1) * stride + x * 3;
            for (unsigned c = 0; c < 3; ++c)
            {
                const auto expected = (unsigned(rgba[2 - c]) * rgba[3] + 255 * (255 - rgba[3]) + 127) / 255;
                if (pixel[c] != expected) std::fprintf(stderr, "PNG mismatch type%u depth%u interlace%u src%ux%u dst%ux%u x%u y%u source%u,%u c%u actual%u expected%u\n", input.png[25], input.png[24], input.png[28], input.width, input.height, w, h, x, y, sx, sy, c, pixel[c], expected);
                assert(pixel[c] == expected);
            }
        }
    assert(!image_fake::live && !image_fake::internal_allocations);
}
int main()
{
    image_fake::psram = true;
    for (unsigned color : {0U, 2U, 3U, 4U, 6U})
        for (unsigned depth : {1U, 2U, 4U, 8U, 16U})
        {
            if ((color == 2 || color == 4 || color == 6) && depth < 8) continue;
            if (color == 3 && depth == 16) continue;
            for (bool interlace : {false, true})
                for (bool transparent : {false, true})
                {
                    if (transparent && (color == 4 || color == 6)) continue;
                    const auto image = fixture(17, 11, color, depth, interlace, transparent);
                    check(image, 17, 11);
                    check(image, 13, 7);
                    check(image, 7, 13);
                }
        }
    for (unsigned w : {1U, 2U, 3U, 7U})
        for (unsigned h : {1U, 2U, 3U, 7U}) check(fixture(w, h, 6, 8, true), w, h);
    check(fixture(96, 96, 6, 8, true), 200, 120); // dictionary wrap + upscale/cover
    check(fixture(8192, 1, 6, 16, false), 7, 1);  // maximum row allocation + stored-block split
    for (const char* name : {"rgba-96x96.png", "fixed-96x96.png"})
    {
        std::ifstream file(std::string(TRAIL_MATE_PNG_FIXTURE_DIR) + "/" + name, std::ios::binary);
        assert(file);
        Fixture encoded{96, 96, {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}, {}};
        for (unsigned y = 0; y < 96; ++y)
            for (unsigned x = 0; x < 96; ++x)
                encoded.rgba.insert(encoded.rgba.end(), {uint8_t(x * 5), uint8_t(y * 7), uint8_t((x + y) * 13), uint8_t((x + y) % 3 == 0 ? 0 : (x + y) % 3 == 1 ? 128
                                                                                                                                                                : 255)});
        check(encoded, 96, 96);
        check(encoded, 200, 120);
    }
    // Fixed length/distance pair before any output: no undefined dictionary
    // history may be read, even if the output ring itself is initialized.
    Bytes deflate{0x78, 0x01};
    unsigned bit = 0;
    const auto bits = [&](unsigned value, unsigned count)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            if (!(bit % 8)) deflate.push_back(0);
            deflate.back() |= uint8_t(((value >> i) & 1) << (bit % 8));
            ++bit;
        }
    };
    bits(3, 3);
    bits(64, 7);
    bits(0, 5);
    bits(0, 7);
    big(deflate, 0x00030001);
    auto inflater = std::make_unique<lgfx_tinfl_decompressor>();
    lgfx_tinfl_init(inflater.get());
    Bytes history(32768, 0xad);
    size_t input_bytes = deflate.size(), output_bytes = history.size();
    assert(trail_mate_tinfl_decompress(inflater.get(), deflate.data(), &input_bytes, history.data(), history.data(), &output_bytes, TINFL_FLAG_HAS_MORE_INPUT | TINFL_FLAG_PARSE_ZLIB_HEADER, 0) < TINFL_STATUS_DONE);
    assert(!output_bytes && std::all_of(history.begin(), history.end(), [](uint8_t b)
                                        { return b == 0xad; }));
    const auto good = fixture(17, 11, 3, 8, true, true);
    const auto allocation_start = image_fake::allocations;
    const auto yield_start = yields;
    check(good, 17, 11);
    const auto allocation_count = image_fake::allocations - allocation_start;
    const auto full_yields = yields - yield_start;
    for (unsigned fail = 1; fail <= allocation_count; ++fail)
    {
        image_fake::fail_at = image_fake::allocations + fail;
        assert(!generate(good.png, 17, 11) && !image_fake::files.count("/cache.bmp") && !image_fake::files.count("/cache.bmp.tmp") && !image_fake::live);
        image_fake::fail_at = 0;
    }
    for (unsigned cut = 1; cut <= 15; ++cut)
    {
        auto broken = good.png;
        broken.resize(broken.size() - cut);
        assert(!generate(broken, 17, 11) && !image_fake::files.count("/cache.bmp"));
    }
    auto bad_crc = good.png;
    bad_crc[29] ^= 1;
    assert(!generate(bad_crc, 17, 11));
    auto trailing = good.png;
    trailing.push_back(0);
    assert(!generate(trailing, 17, 11));
    for (unsigned fault = 1; fault <= 5; ++fault)
    {
        const bool accepted = generate(fixture(17, 11, 6, 8, false, false, fault).png, 17, 11);
        if (accepted) std::fprintf(stderr, "Malformed raster accepted fault%u\n", fault);
        assert(!accepted);
    }
    Bytes injected;
    chunk(injected, "IHDR", {good.png.begin() + 16, good.png.begin() + 29});
    auto duplicate = good.png;
    duplicate.insert(duplicate.begin() + 33, injected.begin(), injected.end());
    assert(!generate(duplicate, 17, 11));
    injected.clear();
    chunk(injected, "ABCD", {});
    auto unknown = good.png;
    unknown.insert(unknown.begin() + 33, injected.begin(), injected.end());
    assert(!generate(unknown, 17, 11));
    // Valid CRC but palette index beyond the declared palette must fail.
    auto palette_bad = fixture(17, 11, 3, 8, false).png;
    const auto palette_size = 16 * 3;
    injected.clear();
    chunk(injected, "PLTE", {palette_bad.begin() + 41, palette_bad.begin() + 44});
    palette_bad.erase(palette_bad.begin() + 33, palette_bad.begin() + 33 + 12 + palette_size);
    palette_bad.insert(palette_bad.begin() + 33, injected.begin(), injected.end());
    assert(!generate(palette_bad, 17, 11));
    // Illegal IDAT ordering; ancillary chunks may follow but never split IDAT.
    auto split = good.png;
    const size_t first_data_end = 33 + 12 + 48 + 12 + 16 + 12 + 12 + 137;
    injected.clear();
    chunk(injected, "tEXt", {});
    split.insert(split.begin() + first_data_end, injected.begin(), injected.end());
    assert(!generate(split, 17, 11));
    auto oversized = good.png;
    oversized.resize(8U * 1024U * 1024U + 1);
    assert(!generate(oversized, 17, 11));
    auto huge = good.png;
    huge[16] = 0;
    huge[17] = 0;
    huge[18] = 0x40;
    huge[19] = 0;
    const auto header_crc = crc(huge.data() + 12, 17);
    for (unsigned i = 0; i < 4; ++i) huge[29 + i] = uint8_t(header_crc >> (24 - i * 8));
    assert(!generate(huge, 17, 11));
    Bytes too_many(good.png.begin(), good.png.begin() + 33);
    for (unsigned i = 0; i < 65536; ++i) chunk(too_many, "tEXt", {});
    too_many.insert(too_many.end(), good.png.begin() + 33, good.png.end());
    assert(!generate(too_many, 17, 11));
    for (unsigned failure = 0; failure < 3; ++failure)
    {
        image_fake::write_fail = failure == 0;
        image_fake::flush_fail = failure == 1;
        image_fake::rename_fail = failure == 2;
        assert(!generate(good.png, 17, 11) && !image_fake::files.count("/cache.bmp") && !image_fake::files.count("/cache.bmp.tmp") && !image_fake::live);
        image_fake::write_fail = image_fake::flush_fail = image_fake::rename_fail = false;
    }
    const auto before = image_fake::allocations;
    image_fake::psram = false;
    assert(!generate(good.png, 17, 11) && image_fake::allocations == before);
    image_fake::psram = true;
    change_at = yields + 3;
    assert(!generate(good.png, 17, 11) && !image_fake::files.count("/cache.bmp") && !image_fake::live);
    change_at = 0;
    image_fake::media = 1;
    change_at = yields + full_yields - 1;
    assert(!generate(good.png, 17, 11) && !image_fake::files.count("/cache.bmp") && !image_fake::live);
    change_at = 0;
    image_fake::media = 1;
    std::ifstream jpeg(TRAIL_MATE_JPEG_FIXTURE, std::ios::binary);
    assert(jpeg);
    const Bytes photo{std::istreambuf_iterator<char>(jpeg), std::istreambuf_iterator<char>()};
    assert(generate(photo, 16, 8));
    const auto& bmp = image_fake::files.at("/cache.bmp");
    for (unsigned i = 54; i < bmp.size(); i += 3)
    {
        if (!(bmp[i] < 30 && bmp[i + 1] < 30 && bmp[i + 2] > 220)) std::fprintf(stderr, "JPEG bgr %u %u %u at %u\n", bmp[i], bmp[i + 1], bmp[i + 2], i);
        assert(bmp[i] < 30 && bmp[i + 1] < 30 && bmp[i + 2] > 220);
    }
    assert(!image_fake::live && !image_fake::internal_allocations && yields);
    std::puts("PNG true pixels/CRC/order/filters/depths/Adam7/transparency/crop/OOM/media and real JPEG cache passed");
}
