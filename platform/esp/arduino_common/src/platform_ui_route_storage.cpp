#include "platform/ui/route_storage.h"

#include "platform/esp/arduino_common/route_image_source_runtime.h"
#include "platform/esp/arduino_common/route_png_cache.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/http_client_runtime.h"
#include "platform/ui/wifi_access_runtime.h"
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
#include "hostlink/route_storage_lease.h"
#include "platform/esp/arduino_common/route_package_image_runtime.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "src/libs/tjpgd/tjpgd.h"
#else
#include "esp_rom_tjpgd.h"
#endif
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace platform::ui::route_storage
{
namespace
{

constexpr const char* kRouteDir = "/routes";
constexpr const char* kRouteAssetRoot = "/routes/.trailmate";
constexpr const char* kRouteAssetImageSubdir = "images";
constexpr const char* kRouteAssetThumbSubdir = "thumbs";
constexpr const char* kRouteAssetViewSubdir = "views";
constexpr std::size_t kRouteDownloadBufferSize = 512;
constexpr int kRouteDownloadTxBufferSize = 512;
constexpr uint32_t kRouteImageWorkerStackBytes = 6 * 1024;
constexpr UBaseType_t kRouteImageWorkerPriority = 2;
constexpr uint16_t kRouteThumbWidth = 200;
constexpr uint16_t kRouteThumbHeight = 120;
constexpr uint16_t kRouteViewWidth = 320;
constexpr uint16_t kRouteViewHeight = 180;
constexpr std::size_t kRouteCacheInternalReserveBytes = 96 * 1024;
constexpr std::size_t kRouteCacheInternalSlackBytes = 8 * 1024;
constexpr std::size_t kRouteJpegDecoderWorkBytes = 3100;
constexpr std::size_t kRouteHttpInternalFreeTargetBytes = 18 * 1024;
constexpr std::size_t kRouteHttpInternalLargestTargetBytes = 8 * 1024;
constexpr std::size_t kRouteImageDownloadAttempts = 2;
constexpr int kRouteImageMemoryWaitAttempts = 16;
constexpr TickType_t kRouteImageRetryDelayTicks = pdMS_TO_TICKS(1600);
constexpr TickType_t kRouteImageMemoryWaitTicks = pdMS_TO_TICKS(250);
constexpr std::uint32_t kRouteImageDownloadMaxBytes = 4U * 1024U * 1024U;
constexpr std::uint32_t kRouteImageProgressMinBytes = 8 * 1024;
constexpr TickType_t kRouteImageProgressMinTicks = pdMS_TO_TICKS(700);

enum class RouteImageBatchKind : uint8_t
{
    Download = 0,
    Cache,
};

struct RouteImageBatchContext
{
#if defined(ESP_PLATFORM)
    static void* allocate_storage(std::size_t size) noexcept
    {
        void* ptr = heap_caps_malloc_prefer(size,
                                            2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return ptr;
    }

    void* operator new(std::size_t size)
    {
        void* ptr = allocate_storage(size);
        return ptr != nullptr ? ptr : ::operator new(size);
    }

    void* operator new(std::size_t size, const std::nothrow_t&) noexcept
    {
        return allocate_storage(size);
    }

    void operator delete(void* ptr) noexcept
    {
        heap_caps_free(ptr);
    }

    void operator delete(void* ptr, std::size_t) noexcept
    {
        operator delete(ptr);
    }

    void operator delete(void* ptr, const std::nothrow_t&) noexcept
    {
        operator delete(ptr);
    }
#endif

    RouteImageBatchKind kind = RouteImageBatchKind::Download;
    std::string asset_id{};
    std::string package_route_path{};
    std::vector<RouteImageDownloadItem> download_items{};
    std::vector<RouteImageCacheItem> cache_items{};
};

struct RouteImageBatchRuntime
{
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t worker_task = nullptr;
    bool launch_pending = false;
    RouteImageTaskPresentation presentation = RouteImageTaskPresentation::Hidden;
    RouteImageDownloadStatus status{};
};

RouteImageBatchRuntime s_image_batch{};

RouteImageDownloadResult download_route_image_with_progress(
    const std::string& url,
    const std::string& output_path,
    std::uint32_t max_bytes,
    platform::ui::http_client::ProgressCallback progress,
    void* progress_context);

bool route_image_source_matches_reference(const std::string& image, const std::string& source)
{
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    hostlink::route_storage_lease::Guard lease;
    if (!lease.acquired()) return false;
#endif
    std::unique_ptr<platform::esp::route_images::RouteImageSourceWork> work(new (std::nothrow) platform::esp::route_images::RouteImageSourceWork);
    return work && work->matchesExistingReference(image, source);
}

bool has_kml_extension(const std::string& name)
{
    if (name.size() < 4)
    {
        return false;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return lower.compare(lower.size() - 4, 4, ".kml") == 0;
}

bool is_safe_asset_id(const std::string& asset_id)
{
    if (asset_id.empty() || (asset_id.size() > 48 && !track_sharing::isInstalledPackageAssetId(asset_id)))
    {
        return false;
    }
    for (unsigned char ch : asset_id)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
        {
            continue;
        }
        return false;
    }
    return true;
}

bool parse_route_image_name(const char* name,
                            std::size_t max_count,
                            std::size_t& out_index)
{
    out_index = 0;
    if (!name || max_count == 0)
    {
        return false;
    }
    if (std::strlen(name) != 12 || std::strncmp(name, "img-", 4) != 0)
    {
        return false;
    }
    std::uint32_t value = 0;
    for (int offset = 4; offset < 8; ++offset)
    {
        const char ch = name[offset];
        if (ch < '0' || ch > '9')
        {
            return false;
        }
        value = (value * 10U) + static_cast<std::uint32_t>(ch - '0');
    }
    if (name[8] != '.' ||
        std::tolower(static_cast<unsigned char>(name[9])) != 'j' ||
        std::tolower(static_cast<unsigned char>(name[10])) != 'p' ||
        std::tolower(static_cast<unsigned char>(name[11])) != 'g' ||
        name[12] != '\0' ||
        value == 0 ||
        value > max_count)
    {
        return false;
    }
    out_index = static_cast<std::size_t>(value - 1U);
    return true;
}

bool ensure_dir(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return false;
    }
    return ::platform::esp::arduino_common::storage::sd_is_directory(path) ||
           ::platform::esp::arduino_common::storage::sd_mkdir(path) ||
           ::platform::esp::arduino_common::storage::sd_is_directory(path);
}

bool ensure_parent_dir(const std::string& path)
{
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0)
    {
        return false;
    }
    return ensure_dir(path.substr(0, slash).c_str());
}

bool ensure_batch_mutex()
{
    if (s_image_batch.mutex != nullptr)
    {
        return true;
    }
    s_image_batch.mutex = xSemaphoreCreateMutex();
    return s_image_batch.mutex != nullptr;
}

class BatchStateLock
{
  public:
    explicit BatchStateLock(SemaphoreHandle_t mutex) : mutex_(mutex)
    {
        if (mutex_)
        {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
    }

    ~BatchStateLock()
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
    }

    BatchStateLock(const BatchStateLock&) = delete;
    BatchStateLock& operator=(const BatchStateLock&) = delete;

  private:
    SemaphoreHandle_t mutex_ = nullptr;
};

void put_le16(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void put_le32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

class HeapCapsBuffer
{
  public:
    HeapCapsBuffer() = default;
    ~HeapCapsBuffer()
    {
        reset();
    }

    HeapCapsBuffer(const HeapCapsBuffer&) = delete;
    HeapCapsBuffer& operator=(const HeapCapsBuffer&) = delete;

    bool allocate(std::size_t bytes, bool zero_fill = true)
    {
        reset();
        if (bytes == 0)
        {
            return false;
        }

        data_ = static_cast<uint8_t*>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data_)
        {
            // PSRAM-equipped devices never spill optional image work into the
            // scarce internal heap when external admission fails.
            if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) return false;
            const std::size_t internal_free =
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            const std::size_t internal_largest =
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (internal_free < bytes + kRouteCacheInternalReserveBytes ||
                internal_largest < bytes + kRouteCacheInternalSlackBytes)
            {
                return false;
            }
            data_ = static_cast<uint8_t*>(
                heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (!data_)
        {
            return false;
        }
        size_ = bytes;
        if (zero_fill)
        {
            std::memset(data_, 0, size_);
        }
        return true;
    }

    void reset()
    {
        if (data_)
        {
            heap_caps_free(data_);
            data_ = nullptr;
        }
        size_ = 0;
    }

    uint8_t* data()
    {
        return data_;
    }

    const uint8_t* data() const
    {
        return data_;
    }

    std::size_t size() const
    {
        return size_;
    }

  private:
    uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

void log_cache_memory_skip(const char* label, std::size_t bytes)
{
    std::printf("[RouteImage][cache] skip %s bytes=%u internal_free=%u "
                "internal_largest=%u psram_largest=%u\n",
                label ? label : "-",
                static_cast<unsigned>(bytes),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void log_route_http_memory(const char* stage)
{
    std::printf("[RouteImage][http] %s internal_free=%u internal_largest=%u "
                "psram_free=%u psram_largest=%u\n",
                stage ? stage : "-",
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void log_route_task_create_failed(const char* task_name,
                                  std::size_t item_count,
                                  uint32_t stack_bytes)
{
    std::printf("[RouteImage][task] create_failed name=%s items=%u stack=%u "
                "internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
                task_name ? task_name : "-",
                static_cast<unsigned>(item_count),
                static_cast<unsigned>(stack_bytes),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

bool route_http_memory_ready()
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
               kRouteHttpInternalFreeTargetBytes &&
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
               kRouteHttpInternalLargestTargetBytes;
}

bool wait_for_route_http_memory()
{
    if (route_http_memory_ready())
    {
        return true;
    }

    log_route_http_memory("wait_begin");
    for (int attempt = 0; attempt < kRouteImageMemoryWaitAttempts; ++attempt)
    {
        vTaskDelay(kRouteImageMemoryWaitTicks);
        if (route_http_memory_ready())
        {
            log_route_http_memory("wait_ready");
            return true;
        }
    }
    log_route_http_memory("wait_continue");
    return false;
}

bool route_image_http_error_retryable(const std::string& error)
{
    return error == "Open HTTP request failed" ||
           error == "Fetch HTTP headers failed" ||
           error == "Read HTTP response failed" ||
           error == "Low HTTP memory" ||
           error == "Create HTTP client failed";
}

bool route_image_url_supported(const std::string& url)
{
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos)
    {
        return false;
    }

    std::string scheme = url.substr(0, scheme_end);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    if (scheme != "http" && scheme != "https")
    {
        return false;
    }

    const std::size_t host_start = scheme_end + 3U;
    if (host_start >= url.size())
    {
        return false;
    }
    const char first_host_char = url[host_start];
    if (first_host_char == '/' || first_host_char == ':' ||
        first_host_char == '?' || first_host_char == '#')
    {
        return false;
    }
    return true;
}

void log_route_worker_stack(const char* stage)
{
#if defined(ESP_PLATFORM)
    const UBaseType_t free_words = uxTaskGetStackHighWaterMark(nullptr);
    std::printf("[RouteImage][task] %s stack_free_words=%u stack_free_bytes=%u\n",
                stage ? stage : "-",
                static_cast<unsigned>(free_words),
                static_cast<unsigned>(free_words * sizeof(StackType_t)));
#else
    (void)stage;
#endif
}

struct RouteJpegCacheContext
{
    ::platform::esp::arduino_common::storage::SdRuntimeFile input{
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? ::platform::esp::arduino_common::storage::SdRuntimeMemoryPolicy::PsramOnly : ::platform::esp::arduino_common::storage::SdRuntimeMemoryPolicy::Default};
    uint8_t* pixels = nullptr;
    std::size_t pixel_bytes = 0;
    double crop_x = 0.0;
    double crop_y = 0.0;
    double crop_w = 0.0;
    double crop_h = 0.0;
    uint16_t source_w = 0;
    uint16_t source_h = 0;
    uint16_t target_w = 0;
    uint16_t target_h = 0;
};

#if defined(CONFIG_IDF_TARGET_ESP32P4)
using RouteJpegDecoder = JDEC;
using RouteJpegRect = JRECT;
using RouteJpegInputResult = std::size_t;
using RouteJpegOutputResult = int;
#else
using RouteJpegDecoder = esp_rom_tjpgd_dec_t;
using RouteJpegRect = esp_rom_tjpgd_rect_t;
using RouteJpegInputResult = uint32_t;
using RouteJpegOutputResult = uint32_t;
#endif

RouteJpegInputResult jpeg_input_cb(RouteJpegDecoder* dec,
                                   uint8_t* buffer,
                                   RouteJpegInputResult ndata)
{
    auto* ctx = dec ? static_cast<RouteJpegCacheContext*>(dec->device) : nullptr;
    if (!ctx)
    {
        return 0;
    }
    if (buffer)
    {
        const int read = ctx->input.read(buffer, ndata);
        return read > 0 ? static_cast<RouteJpegInputResult>(read) : 0;
    }

    const uint64_t next = ctx->input.position() + static_cast<uint64_t>(ndata);
    return ctx->input.seek(next) ? ndata : 0;
}

RouteJpegOutputResult jpeg_output_cb(RouteJpegDecoder* dec,
                                     void* bitmap,
                                     RouteJpegRect* rect)
{
    auto* ctx = dec ? static_cast<RouteJpegCacheContext*>(dec->device) : nullptr;
    auto* rgb = static_cast<const uint8_t*>(bitmap);
    if (!ctx || !rgb || !rect || !ctx->pixels ||
        ctx->crop_w <= 0.0 || ctx->crop_h <= 0.0 ||
        ctx->target_w == 0 || ctx->target_h == 0)
    {
        return 0;
    }

    const int rect_w = static_cast<int>(rect->right - rect->left + 1);
    if (rect_w <= 0)
    {
        return 1;
    }

    const int dy0 = std::max<int>(0,
                                  static_cast<int>(std::floor(
                                      ((static_cast<double>(rect->top) - ctx->crop_y) *
                                       ctx->target_h) /
                                      ctx->crop_h)) -
                                      1);
    const int dy1 = std::min<int>(
        static_cast<int>(ctx->target_h) - 1,
        static_cast<int>(std::ceil(
            ((static_cast<double>(rect->bottom + 1U) - ctx->crop_y) * ctx->target_h) /
            ctx->crop_h)) +
            1);
    const int dx0 = std::max<int>(0,
                                  static_cast<int>(std::floor(
                                      ((static_cast<double>(rect->left) - ctx->crop_x) *
                                       ctx->target_w) /
                                      ctx->crop_w)) -
                                      1);
    const int dx1 = std::min<int>(
        static_cast<int>(ctx->target_w) - 1,
        static_cast<int>(std::ceil(
            ((static_cast<double>(rect->right + 1U) - ctx->crop_x) * ctx->target_w) /
            ctx->crop_w)) +
            1);

    for (int dy = dy0; dy <= dy1; ++dy)
    {
        const double syf = ctx->crop_y +
                           ((static_cast<double>(dy) + 0.5) * ctx->crop_h) /
                               ctx->target_h;
        const int sy = std::max<int>(
            0,
            std::min<int>(static_cast<int>(ctx->source_h) - 1,
                          static_cast<int>(syf)));
        if (sy < rect->top || sy > rect->bottom)
        {
            continue;
        }

        for (int dx = dx0; dx <= dx1; ++dx)
        {
            const double sxf = ctx->crop_x +
                               ((static_cast<double>(dx) + 0.5) * ctx->crop_w) /
                                   ctx->target_w;
            const int sx = std::max<int>(
                0,
                std::min<int>(static_cast<int>(ctx->source_w) - 1,
                              static_cast<int>(sxf)));
            if (sx < rect->left || sx > rect->right)
            {
                continue;
            }

            const std::size_t src_offset =
                (static_cast<std::size_t>(sy - rect->top) *
                     static_cast<std::size_t>(rect_w) +
                 static_cast<std::size_t>(sx - rect->left)) *
                3U;
            const std::size_t dst_offset =
                (static_cast<std::size_t>(dy) * ctx->target_w +
                 static_cast<std::size_t>(dx)) *
                3U;
            if (dst_offset + 2U >= ctx->pixel_bytes)
            {
                continue;
            }
            // LVGL's P4 tiny JPEG backend emits BGR; ESP ROM emits RGB.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
            ctx->pixels[dst_offset + 0] = rgb[src_offset + 0];
            ctx->pixels[dst_offset + 1] = rgb[src_offset + 1];
            ctx->pixels[dst_offset + 2] = rgb[src_offset + 2];
#else
            ctx->pixels[dst_offset + 0] = rgb[src_offset + 2];
            ctx->pixels[dst_offset + 1] = rgb[src_offset + 1];
            ctx->pixels[dst_offset + 2] = rgb[src_offset + 0];
#endif
        }
    }
    return 1;
}

void configure_cover_crop(RouteJpegCacheContext& ctx)
{
    const double source_aspect =
        static_cast<double>(ctx.source_w) / static_cast<double>(ctx.source_h);
    const double target_aspect =
        static_cast<double>(ctx.target_w) / static_cast<double>(ctx.target_h);
    if (source_aspect > target_aspect)
    {
        ctx.crop_h = ctx.source_h;
        ctx.crop_w = ctx.crop_h * target_aspect;
        ctx.crop_x = (static_cast<double>(ctx.source_w) - ctx.crop_w) / 2.0;
        ctx.crop_y = 0.0;
    }
    else
    {
        ctx.crop_w = ctx.source_w;
        ctx.crop_h = ctx.crop_w / target_aspect;
        ctx.crop_x = 0.0;
        ctx.crop_y = (static_cast<double>(ctx.source_h) - ctx.crop_h) / 2.0;
    }
}

bool write_bmp_24(const std::string& path,
                  const uint8_t* pixels,
                  std::size_t pixel_size,
                  uint16_t width,
                  uint16_t height)
{
    if (path.empty() || width == 0 || height == 0 ||
        !pixels ||
        pixel_size < static_cast<std::size_t>(width) * height * 3U)
    {
        return false;
    }

    ::platform::esp::arduino_common::storage::SdRuntimeFile out(
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? ::platform::esp::arduino_common::storage::SdRuntimeMemoryPolicy::PsramOnly : ::platform::esp::arduino_common::storage::SdRuntimeMemoryPolicy::Default);
    if (!out.open(path.c_str(), "w"))
    {
        return false;
    }

    const uint32_t row_bytes = (static_cast<uint32_t>(width) * 3U + 3U) & ~3U;
    const uint32_t pixel_bytes = row_bytes * height;
    const uint32_t data_offset = 14U + 40U;
    const uint32_t file_size = data_offset + pixel_bytes;

    uint8_t file_hdr[14] = {'B', 'M'};
    put_le32(file_hdr + 2, file_size);
    put_le32(file_hdr + 10, data_offset);
    if (out.write(file_hdr, sizeof(file_hdr)) != sizeof(file_hdr))
    {
        out.close();
        return false;
    }

    uint8_t info_hdr[40]{};
    put_le32(info_hdr + 0, 40U);
    put_le32(info_hdr + 4, width);
    put_le32(info_hdr + 8, height);
    put_le16(info_hdr + 12, 1U);
    put_le16(info_hdr + 14, 24U);
    put_le32(info_hdr + 20, pixel_bytes);
    if (out.write(info_hdr, sizeof(info_hdr)) != sizeof(info_hdr))
    {
        out.close();
        return false;
    }

    HeapCapsBuffer row;
    if (!row.allocate(row_bytes)) return false;
    const std::size_t src_row_bytes = static_cast<std::size_t>(width) * 3U;
    for (uint16_t row_index = 0; row_index < height; ++row_index)
    {
        const uint16_t source_y = static_cast<uint16_t>(height - 1U - row_index);
        const uint8_t* src = pixels +
                             static_cast<std::size_t>(source_y) * src_row_bytes;
        std::memcpy(row.data(), src, src_row_bytes);
        if (out.write(row.data(), row.size()) != row.size())
        {
            out.close();
            return false;
        }
    }

    const bool flushed = out.flush();
    out.close();
    return flushed;
}

bool generate_route_image_bmp_cache(const std::string& source_path,
                                    const std::string& output_path,
                                    uint16_t width,
                                    uint16_t height)
{
    if (source_path.empty() || output_path.empty() || width == 0 || height == 0)
    {
        return false;
    }
    if (route_asset_file_exists(output_path))
    {
        return true;
    }
    if (!ensure_parent_dir(output_path))
    {
        return false;
    }

    RouteJpegCacheContext ctx{};
    ctx.target_w = width;
    ctx.target_h = height;
    if (width > kRouteViewWidth || height > kRouteViewHeight || !ctx.input.open(source_path.c_str(), "r")) return false;
    uint8_t magic[8]{};
    const int magic_bytes = ctx.input.read(magic, sizeof(magic));
    if (magic_bytes != sizeof(magic) || !ctx.input.seek(0)) return false;
    constexpr uint8_t png_magic[] = {137, 80, 78, 71, 13, 10, 26, 10};
    const bool png = std::memcmp(magic, png_magic, sizeof(magic)) == 0;
    if (png && !heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) return false;
    HeapCapsBuffer pixels;
    const std::size_t pixel_bytes = static_cast<std::size_t>(width) * height * 3U;
    if (!pixels.allocate(pixel_bytes))
    {
        log_cache_memory_skip("pixel_alloc", pixel_bytes);
        return false;
    }
    ctx.pixels = pixels.data();
    ctx.pixel_bytes = pixels.size();
    bool decoded = false;
    if (png)
    {
        decoded = platform::esp::route_images::decodeRoutePng(ctx.input, pixels.data(), pixels.size(), width, height);
    }
    else
    {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
        constexpr uint8_t kDecodeScale = 0;
#else
        constexpr uint8_t kDecodeScale = 1;
#endif
        HeapCapsBuffer decoder_work;
        if (!decoder_work.allocate(kRouteJpegDecoderWorkBytes))
        {
            ctx.input.close();
            log_cache_memory_skip("decoder_work", kRouteJpegDecoderWorkBytes);
            return false;
        }
        RouteJpegDecoder decoder{};
#if defined(CONFIG_IDF_TARGET_ESP32P4)
        JRESULT result = jd_prepare(&decoder,
                                    jpeg_input_cb,
                                    decoder_work.data(),
                                    decoder_work.size(),
                                    &ctx);
#else
        esp_rom_tjpgd_result_t result =
            esp_rom_tjpgd_prepare(&decoder,
                                  jpeg_input_cb,
                                  decoder_work.data(),
                                  static_cast<uint32_t>(decoder_work.size()),
                                  &ctx);
#endif
        if (result != JDR_OK || decoder.width == 0 || decoder.height == 0)
        {
            ctx.input.close();
            return false;
        }
        ctx.source_w = static_cast<uint16_t>(
            std::max<uint32_t>(1U, decoder.width >> kDecodeScale));
        ctx.source_h = static_cast<uint16_t>(
            std::max<uint32_t>(1U, decoder.height >> kDecodeScale));
        configure_cover_crop(ctx);

#if defined(CONFIG_IDF_TARGET_ESP32P4)
        result = jd_decomp(&decoder, jpeg_output_cb, kDecodeScale);
#else
        result = esp_rom_tjpgd_decomp(&decoder, jpeg_output_cb, kDecodeScale);
#endif
        decoded = result == JDR_OK;
    }
    ctx.input.close();
    if (!decoded) return false;

    const std::string temp_path = output_path + ".tmp";
    (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
    if (!write_bmp_24(temp_path, pixels.data(), pixels.size(), width, height))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }
    if (::platform::esp::arduino_common::storage::sd_exists(output_path.c_str()))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(output_path.c_str());
    }
    if (!::platform::esp::arduino_common::storage::sd_rename(temp_path.c_str(), output_path.c_str()))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }
    return true;
}

bool route_image_cache_ready(const RouteImageCacheItem& item)
{
    return !item.source_path.empty() &&
           route_asset_file_exists(item.source_path) &&
           (item.preview_path.empty() || route_asset_file_exists(item.preview_path));
}

bool ensure_route_image_cache(const RouteImageCacheItem& item)
{
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    hostlink::route_storage_lease::Guard lease;
    if (!lease.acquired()) return false;
#endif
    if (route_image_cache_ready(item))
    {
        return true;
    }
    if (item.source_path.empty() || !route_asset_file_exists(item.source_path))
    {
        return false;
    }

    bool ok = true;
    if (!item.preview_path.empty() && !route_asset_file_exists(item.preview_path))
    {
        ok = generate_route_image_bmp_cache(
                 item.source_path,
                 item.preview_path,
                 kRouteThumbWidth,
                 kRouteThumbHeight) &&
             ok;
    }
    if (!ok)
    {
        return false;
    }
    if (!item.view_path.empty() && !route_asset_file_exists(item.view_path))
    {
        (void)generate_route_image_bmp_cache(item.source_path,
                                             item.view_path,
                                             kRouteViewWidth,
                                             kRouteViewHeight);
    }
    return route_image_cache_ready(item);
}

void set_batch_status_locked(RouteImageDownloadPhase phase,
                             bool busy,
                             const std::string& asset_id,
                             std::size_t total,
                             std::size_t processed,
                             std::size_t saved,
                             std::size_t failed,
                             std::size_t current_index,
                             std::uint32_t bytes,
                             const char* message,
                             const char* error,
                             std::uint32_t current_bytes = 0,
                             std::uint32_t current_total_bytes = 0)
{
    s_image_batch.status.phase = phase;
    s_image_batch.status.busy = busy;
    s_image_batch.status.presentation = s_image_batch.presentation;
    s_image_batch.status.asset_id = asset_id;
    s_image_batch.status.total = total;
    s_image_batch.status.processed = processed;
    s_image_batch.status.saved = saved;
    s_image_batch.status.failed = failed;
    s_image_batch.status.current_index = current_index;
    s_image_batch.status.bytes = bytes;
    s_image_batch.status.current_bytes = current_bytes;
    s_image_batch.status.current_total_bytes = current_total_bytes;
    s_image_batch.status.message = message ? message : "";
    s_image_batch.status.error = error ? error : "";
}

void update_batch_status(RouteImageDownloadPhase phase,
                         bool busy,
                         const std::string& asset_id,
                         std::size_t total,
                         std::size_t processed,
                         std::size_t saved,
                         std::size_t failed,
                         std::size_t current_index,
                         std::uint32_t bytes,
                         const char* message,
                         const char* error = nullptr,
                         std::uint32_t current_bytes = 0,
                         std::uint32_t current_total_bytes = 0)
{
    if (!ensure_batch_mutex())
    {
        return;
    }
    BatchStateLock lock(s_image_batch.mutex);
    set_batch_status_locked(phase,
                            busy,
                            asset_id,
                            total,
                            processed,
                            saved,
                            failed,
                            current_index,
                            bytes,
                            message,
                            error,
                            current_bytes,
                            current_total_bytes);
}

void promote_batch_presentation_locked(RouteImageTaskPresentation presentation)
{
    if (presentation == RouteImageTaskPresentation::UserVisible)
    {
        s_image_batch.presentation = presentation;
        s_image_batch.status.presentation = presentation;
    }
}

struct RouteImageProgressContext
{
    const std::string* asset_id = nullptr;
    std::size_t total = 0;
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::size_t index = 0;
    std::uint32_t base_bytes = 0;
    std::uint32_t last_bytes = 0;
    TickType_t last_tick = 0;
};

class RouteImageForegroundDownloadScope
{
  public:
    RouteImageForegroundDownloadScope() = default;
    ~RouteImageForegroundDownloadScope()
    {
        end();
    }

    RouteImageForegroundDownloadScope(const RouteImageForegroundDownloadScope&) = delete;
    RouteImageForegroundDownloadScope& operator=(const RouteImageForegroundDownloadScope&) = delete;

    void begin()
    {
        if (active_)
        {
            return;
        }

        platform::ui::wifi_access::Request request{};
        request.client = platform::ui::wifi_access::Client::RouteStorage;
        request.kind = platform::ui::wifi_access::AccessKind::HttpDownload;
        request.priority = platform::ui::wifi_access::Priority::UserForeground;
        request.reason = "route_image_pending";
        platform::ui::wifi_access::Decision decision =
            platform::ui::wifi_access::Decision::Granted;
        active_ = platform::ui::wifi_access::begin_foreground_download(
            request,
            &decision);
        if (!active_)
        {
            std::printf("[RouteImage][wifi] pending denied decision=%s\n",
                        platform::ui::wifi_access::decision_name(decision));
        }
    }

    void end()
    {
        if (!active_)
        {
            return;
        }
        platform::ui::wifi_access::end_foreground_download(
            platform::ui::wifi_access::Client::RouteStorage,
            platform::ui::wifi_access::AccessKind::HttpDownload);
        active_ = false;
    }

  private:
    bool active_ = false;
};

void route_image_download_progress(std::uint32_t current_bytes,
                                   std::uint32_t total_bytes,
                                   void* context)
{
    auto* progress = static_cast<RouteImageProgressContext*>(context);
    if (!progress || !progress->asset_id)
    {
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    const bool first = progress->last_tick == 0;
    const bool enough_bytes =
        current_bytes >= progress->last_bytes &&
        current_bytes - progress->last_bytes >= kRouteImageProgressMinBytes;
    const bool enough_time = now - progress->last_tick >= kRouteImageProgressMinTicks;
    const bool complete = total_bytes > 0 && current_bytes >= total_bytes;
    if (!first && !enough_bytes && !enough_time && !complete)
    {
        return;
    }

    progress->last_tick = now;
    progress->last_bytes = current_bytes;

    char detail[96]{};
    if (total_bytes > 0)
    {
        std::snprintf(detail,
                      sizeof(detail),
                      "Image %u/%u  %u/%u KB",
                      static_cast<unsigned>(progress->index + 1U),
                      static_cast<unsigned>(progress->total),
                      static_cast<unsigned>((current_bytes + 1023U) / 1024U),
                      static_cast<unsigned>((total_bytes + 1023U) / 1024U));
    }
    else
    {
        std::snprintf(detail,
                      sizeof(detail),
                      "Image %u/%u  %u KB",
                      static_cast<unsigned>(progress->index + 1U),
                      static_cast<unsigned>(progress->total),
                      static_cast<unsigned>((current_bytes + 1023U) / 1024U));
    }
    update_batch_status(RouteImageDownloadPhase::Downloading,
                        true,
                        *progress->asset_id,
                        progress->total,
                        progress->processed,
                        progress->saved,
                        progress->failed,
                        progress->index,
                        progress->base_bytes + current_bytes,
                        detail,
                        nullptr,
                        current_bytes,
                        total_bytes);
}

void route_image_worker_task(void* param)
{
    auto* ctx = static_cast<RouteImageBatchContext*>(param);
    std::string asset_id;
    std::string package_route_path;
    RouteImageBatchKind kind = RouteImageBatchKind::Download;
    std::vector<RouteImageDownloadItem> download_items;
    std::vector<RouteImageCacheItem> cache_items;
    if (ctx != nullptr)
    {
        kind = ctx->kind;
        asset_id = std::move(ctx->asset_id);
        package_route_path = std::move(ctx->package_route_path);
        download_items = std::move(ctx->download_items);
        cache_items = std::move(ctx->cache_items);
        delete ctx;
    }

    const bool cache_only = kind == RouteImageBatchKind::Cache;
    const std::size_t total = cache_only ? cache_items.size() : download_items.size();
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::uint32_t bytes = 0;
    std::size_t consecutive_connection_failures = 0;
    bool stopped_for_connection_failure = false;
    char detail[96]{};

    log_route_worker_stack("start");
    std::snprintf(detail,
                  sizeof(detail),
                  cache_only ? "Caching 0/%u" : "Downloading 0/%u",
                  static_cast<unsigned>(total));
    update_batch_status(cache_only ? RouteImageDownloadPhase::Caching
                                   : RouteImageDownloadPhase::Downloading,
                        true,
                        asset_id,
                        total,
                        processed,
                        saved,
                        failed,
                        0,
                        bytes,
                        detail);

    std::string asset_dir;
    if (!ensure_route_asset_dir(asset_id, asset_dir))
    {
        update_batch_status(RouteImageDownloadPhase::Failed,
                            false,
                            asset_id,
                            total,
                            processed,
                            saved,
                            total,
                            0,
                            bytes,
                            "Create image dir failed",
                            "Create image dir failed");
    }
    else if (cache_only)
    {
        uint64_t package_sources = 0;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
        if (!package_route_path.empty())
        {
            namespace images = platform::esp::route_images;
            auto* work = images::createRoutePackageImageWork();
            if (work)
            {
                auto checked = work->begin(package_route_path);
                while (checked == track_sharing::ManifestFileResult::Pending)
                {
                    checked = work->step();
                    taskYIELD();
                }
                if (work->ready())
                    for (size_t i = 0; i < cache_items.size() && i < 64; ++i)
                        if (cache_items[i].source_hash_valid && work->materialize(cache_items[i].source_hash, cache_items[i].source_path, []
                                                                                  { taskYIELD(); })) package_sources |= uint64_t(1) << i;
                // Release the package lease and proof/mapping work before the
                // existing decoder acquires its own lease and pixel workspace.
                images::destroyRoutePackageImageWork(work);
            }
        }
#endif
        for (std::size_t index = 0; index < total; ++index)
        {
            const RouteImageCacheItem& item = cache_items[index];
            std::snprintf(detail,
                          sizeof(detail),
                          "Caching %u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            update_batch_status(RouteImageDownloadPhase::Caching,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);

            ++processed;
            if ((package_route_path.empty() || (index < 64 && (package_sources & (uint64_t(1) << index)))) && ensure_route_image_cache(item))
            {
                ++saved;
                std::snprintf(detail,
                              sizeof(detail),
                              "Cached %u/%u",
                              static_cast<unsigned>(saved),
                              static_cast<unsigned>(total));
            }
            else
            {
                ++failed;
                std::snprintf(detail,
                              sizeof(detail),
                              "Cache %u failed",
                              static_cast<unsigned>(index + 1));
            }

            update_batch_status(RouteImageDownloadPhase::Caching,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);
        }

        const bool ok = failed == 0 && saved >= total;
        if (ok)
        {
            std::snprintf(detail,
                          sizeof(detail),
                          "Cached all %u images",
                          static_cast<unsigned>(total));
        }
        else
        {
            std::snprintf(detail,
                          sizeof(detail),
                          "Cached %u/%u  failed %u",
                          static_cast<unsigned>(saved),
                          static_cast<unsigned>(total),
                          static_cast<unsigned>(failed));
        }
        update_batch_status(ok ? RouteImageDownloadPhase::Done : RouteImageDownloadPhase::Failed,
                            false,
                            asset_id,
                            total,
                            processed,
                            saved,
                            failed,
                            processed < total ? processed : (total == 0 ? 0 : total - 1),
                            bytes,
                            detail,
                            ok ? nullptr : detail);
    }
    else
    {
        RouteImageForegroundDownloadScope foreground_download;
        foreground_download.begin();
        for (std::size_t index = 0; index < total;)
        {
            const RouteImageDownloadItem& item = download_items[index];
            bool original_available = false;
            bool defer_for_memory = false;
            std::snprintf(detail,
                          sizeof(detail),
                          "Image %u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            update_batch_status(RouteImageDownloadPhase::Downloading,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);

            if (item.output_path.empty() || item.url.empty())
            {
                ++failed;
                consecutive_connection_failures = 0;
                std::snprintf(detail,
                              sizeof(detail),
                              "Image %u missing URL",
                              static_cast<unsigned>(index + 1));
            }
            else if (route_image_source_matches_reference(item.output_path, item.url))
            {
                original_available = true;
                consecutive_connection_failures = 0;
                std::snprintf(detail,
                              sizeof(detail),
                              "Image %u/%u already saved",
                              static_cast<unsigned>(index + 1),
                              static_cast<unsigned>(total));
            }
            else
            {
                RouteImageDownloadResult result{};
                RouteImageProgressContext progress_context{};
                progress_context.asset_id = &asset_id;
                progress_context.total = total;
                progress_context.processed = processed;
                progress_context.saved = saved;
                progress_context.failed = failed;
                progress_context.index = index;
                progress_context.base_bytes = bytes;
                for (std::size_t attempt = 0; attempt < kRouteImageDownloadAttempts; ++attempt)
                {
                    while (!wait_for_route_http_memory())
                    {
                        std::snprintf(detail,
                                      sizeof(detail),
                                      "Waiting memory %u/%u",
                                      static_cast<unsigned>(index + 1),
                                      static_cast<unsigned>(total));
                        update_batch_status(RouteImageDownloadPhase::Downloading,
                                            true,
                                            asset_id,
                                            total,
                                            processed,
                                            saved,
                                            failed,
                                            index,
                                            bytes,
                                            detail);
                        log_route_http_memory("tight_wait");
                        vTaskDelay(kRouteImageRetryDelayTicks);
                    }
                    progress_context.last_bytes = 0;
                    progress_context.last_tick = 0;
                    result = download_route_image_with_progress(item.url,
                                                                item.output_path,
                                                                kRouteImageDownloadMaxBytes,
                                                                route_image_download_progress,
                                                                &progress_context);
                    if (!result.ok && result.error == "Low HTTP memory")
                    {
                        defer_for_memory = true;
                        std::snprintf(detail,
                                      sizeof(detail),
                                      "Waiting memory %u/%u",
                                      static_cast<unsigned>(index + 1),
                                      static_cast<unsigned>(total));
                        update_batch_status(RouteImageDownloadPhase::Downloading,
                                            true,
                                            asset_id,
                                            total,
                                            processed,
                                            saved,
                                            failed,
                                            index,
                                            bytes,
                                            detail);
                        log_route_http_memory("http_low_memory");
                        vTaskDelay(kRouteImageRetryDelayTicks);
                        break;
                    }
                    if (result.ok ||
                        !route_image_http_error_retryable(result.error) ||
                        attempt + 1U >= kRouteImageDownloadAttempts)
                    {
                        break;
                    }

                    std::snprintf(detail,
                                  sizeof(detail),
                                  "Retry %u/%u image %u",
                                  static_cast<unsigned>(attempt + 2U),
                                  static_cast<unsigned>(kRouteImageDownloadAttempts),
                                  static_cast<unsigned>(index + 1));
                    update_batch_status(RouteImageDownloadPhase::Downloading,
                                        true,
                                        asset_id,
                                        total,
                                        processed,
                                        saved,
                                        failed,
                                        index,
                                        bytes,
                                        detail);
                    log_route_http_memory("retry_wait");
                    vTaskDelay(kRouteImageRetryDelayTicks);
                }
                if (defer_for_memory)
                {
                    consecutive_connection_failures = 0;
                    continue;
                }
                else if (result.ok)
                {
                    original_available = true;
                    bytes += result.bytes;
                    consecutive_connection_failures = 0;
                }
                else
                {
                    ++failed;
                    if (route_image_http_error_retryable(result.error))
                    {
                        ++consecutive_connection_failures;
                    }
                    else
                    {
                        consecutive_connection_failures = 0;
                    }
                    std::snprintf(detail,
                                  sizeof(detail),
                                  "Image %u failed: %.28s",
                                  static_cast<unsigned>(index + 1),
                                  result.error.empty() ? "Download failed" : result.error.c_str());
                }
            }

            if (original_available)
            {
                ++saved;
                std::snprintf(detail,
                              sizeof(detail),
                              "Saved %u/%u  %u KB",
                              static_cast<unsigned>(saved),
                              static_cast<unsigned>(total),
                              static_cast<unsigned>((bytes + 1023U) / 1024U));
            }
            ++processed;

            update_batch_status(RouteImageDownloadPhase::Downloading,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);

            if (consecutive_connection_failures >= 3)
            {
                stopped_for_connection_failure = true;
                std::snprintf(detail,
                              sizeof(detail),
                              "Connection failed; stopped at %u/%u",
                              static_cast<unsigned>(index + 1),
                              static_cast<unsigned>(total));
                break;
            }
            ++index;
        }

        const bool ok = !stopped_for_connection_failure && failed == 0 && saved >= total;
        if (!stopped_for_connection_failure)
        {
            if (ok)
            {
                std::snprintf(detail,
                              sizeof(detail),
                              "Saved all %u images",
                              static_cast<unsigned>(total));
            }
            else
            {
                std::snprintf(detail,
                              sizeof(detail),
                              "Saved %u/%u  failed %u",
                              static_cast<unsigned>(saved),
                              static_cast<unsigned>(total),
                              static_cast<unsigned>(failed));
            }
        }
        update_batch_status(ok ? RouteImageDownloadPhase::Done : RouteImageDownloadPhase::Failed,
                            false,
                            asset_id,
                            total,
                            processed,
                            saved,
                            failed,
                            processed < total ? processed : (total == 0 ? 0 : total - 1),
                            bytes,
                            detail,
                            ok ? nullptr : detail);
    }

    if (ensure_batch_mutex())
    {
        BatchStateLock lock(s_image_batch.mutex);
        s_image_batch.worker_task = nullptr;
        s_image_batch.launch_pending = false;
    }
    std::vector<RouteImageDownloadItem>().swap(download_items);
    std::vector<RouteImageCacheItem>().swap(cache_items);
    std::string().swap(asset_id);
    log_route_worker_stack("finish");
    vTaskDelete(nullptr);
}

} // namespace

bool is_supported()
{
    return true;
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    ::platform::esp::arduino_common::storage::SdRuntimeDir dir;
    if (!dir.open(kRouteDir))
    {
        return false;
    }

    char name_buf[128];
    bool is_dir = false;
    while (out_routes.size() < max_count && dir.read_next(name_buf, sizeof(name_buf), &is_dir))
    {
        if (!is_dir)
        {
            std::string name = name_buf;
            if (has_kml_extension(name))
            {
                out_routes.push_back(name);
            }
        }
    }
    dir.close();

    std::sort(out_routes.begin(), out_routes.end());
    return true;
}

bool remove_route(const std::string& path)
{
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    hostlink::route_storage_lease::Guard lease;
    if (!lease.acquired()) return false;
#endif
    if (!platform::ui::device::sd_ready() || path.empty())
    {
        return false;
    }
    return ::platform::esp::arduino_common::storage::sd_remove(path.c_str());
}

const char* route_dir()
{
    return kRouteDir;
}

bool ensure_route_asset_dir(const std::string& asset_id, std::string& out_dir)
{
    out_dir.clear();
    if (!platform::ui::device::sd_ready() || !is_safe_asset_id(asset_id))
    {
        return false;
    }
    if (!ensure_dir(kRouteDir) || !ensure_dir(kRouteAssetRoot))
    {
        return false;
    }
    out_dir = std::string(kRouteAssetRoot) + "/" + asset_id;
    if (!ensure_dir(out_dir.c_str()))
    {
        out_dir.clear();
        return false;
    }
    const std::string image_dir = out_dir + "/" + kRouteAssetImageSubdir;
    const std::string thumb_dir = out_dir + "/" + kRouteAssetThumbSubdir;
    const std::string view_dir = out_dir + "/" + kRouteAssetViewSubdir;
    if (!ensure_dir(image_dir.c_str()) ||
        !ensure_dir(thumb_dir.c_str()) ||
        !ensure_dir(view_dir.c_str()))
    {
        out_dir.clear();
        return false;
    }
    return true;
}

bool count_route_saved_images(const std::string& asset_id,
                              std::size_t max_count,
                              std::size_t& out_count)
{
    out_count = 0;
    if (!platform::ui::device::sd_ready() ||
        !is_safe_asset_id(asset_id) ||
        max_count == 0)
    {
        return false;
    }

    const std::string image_dir =
        std::string(kRouteAssetRoot) + "/" + asset_id + "/" + kRouteAssetImageSubdir;
    ::platform::esp::arduino_common::storage::SdRuntimeDir dir;
    if (!dir.open(image_dir.c_str()))
    {
        return false;
    }

    std::vector<std::uint8_t> seen(max_count, 0);
    char name_buf[64];
    bool is_dir = false;
    while (out_count < max_count && dir.read_next(name_buf, sizeof(name_buf), &is_dir))
    {
        if (is_dir)
        {
            continue;
        }
        std::size_t index = 0;
        if (parse_route_image_name(name_buf, max_count, index) &&
            index < seen.size() &&
            !seen[index])
        {
            seen[index] = 1;
            ++out_count;
        }
    }
    dir.close();
    return true;
}

bool route_asset_file_exists(const std::string& path)
{
    return platform::ui::device::sd_ready() && !path.empty() &&
           ::platform::esp::arduino_common::storage::sd_exists(path.c_str()) &&
           !::platform::esp::arduino_common::storage::sd_is_directory(path.c_str());
}

bool route_image_source_matches(const std::string& path, const uint8_t expected_source_hash[32])
{
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    hostlink::route_storage_lease::Guard lease;
    if (!lease.acquired()) return false;
#endif
    std::unique_ptr<platform::esp::route_images::RouteImageSourceWork> work(new (std::nothrow) platform::esp::route_images::RouteImageSourceWork);
    return work && work->matchesExisting(path, expected_source_hash);
}

bool route_image_reference_hash(std::string_view reference, uint8_t output[32])
{
    std::unique_ptr<platform::esp::route_images::ImageReferenceHashWork> work(new (std::nothrow) platform::esp::route_images::ImageReferenceHashWork);
    return work && work->hash(reference, output);
}

namespace
{

RouteImageDownloadResult download_route_image_with_progress(
    const std::string& url,
    const std::string& output_path,
    std::uint32_t max_bytes,
    platform::ui::http_client::ProgressCallback progress,
    void* progress_context)
{
    RouteImageDownloadResult result{};
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    hostlink::route_storage_lease::Guard lease;
    if (!lease.acquired())
    {
        result.error = "Route storage busy";
        return result;
    }
#endif
    if (url.empty() || output_path.empty())
    {
        result.error = "Missing URL";
        return result;
    }
    if (!route_image_url_supported(url))
    {
        result.error = "Invalid image URL";
        return result;
    }
    if (!platform::ui::device::sd_ready())
    {
        result.error = "No SD card";
        return result;
    }
    if (!ensure_parent_dir(output_path))
    {
        result.error = "Create image dir failed";
        return result;
    }

    if (!max_bytes || max_bytes > track_sharing::kImageSourceByteLimit)
    {
        result.error = "Invalid image size limit";
        return result;
    }
    std::unique_ptr<platform::esp::route_images::RouteImageSourceWork> work(new (std::nothrow) platform::esp::route_images::RouteImageSourceWork);
    if (!work)
    {
        result.error = "Low image verification memory";
        return result;
    }
    if (!work->begin(url, output_path))
    {
        result.error = "Invalid image path or unavailable storage";
        return result;
    }

    platform::ui::http_client::Request request{};
    request.url = url.c_str();
    request.client = platform::ui::wifi_access::Client::RouteStorage;
    request.access_kind = platform::ui::wifi_access::AccessKind::HttpDownload;
    request.priority = platform::ui::wifi_access::Priority::UserForeground;
    request.reason = "route_image";
    request.buffer_size = static_cast<int>(kRouteDownloadBufferSize);
    request.tx_buffer_size = kRouteDownloadTxBufferSize;
    request.max_bytes = max_bytes;
    request.min_internal_free_bytes = kRouteHttpInternalFreeTargetBytes;
    request.min_internal_largest_bytes = kRouteHttpInternalLargestTargetBytes;
    request.progress = progress;
    request.progress_context = progress_context;

    platform::ui::http_client::TransferStats stats{};
    const bool ok = platform::ui::http_client::download(
        request,
        [](const std::uint8_t* data, std::size_t len, void* context)
        {
            auto* work = static_cast<platform::esp::route_images::RouteImageSourceWork*>(context);
            return work && work->feed(data, len);
        },
        work.get(),
        result.error,
        &stats);
    result.http_status = stats.http_status;
    result.bytes = stats.bytes;

    if (!ok) return result;
    if (stats.bytes != work->bytes() || !work->seal())
    {
        result.error = "Image write or hash failed";
        return result;
    }
    while (work->verifying())
    {
        work->verifyStep();
        taskYIELD();
    }
    if (!work->commit())
    {
        result.ok = false;
        result.error = "Image verification or source binding failed";
        return result;
    }

    result.ok = true;
    result.error.clear();
    return result;
}

} // namespace

RouteImageDownloadResult download_route_image(const std::string& url,
                                              const std::string& output_path,
                                              std::uint32_t max_bytes)
{
    return download_route_image_with_progress(url, output_path, max_bytes, nullptr, nullptr);
}

bool start_route_image_download(const std::string& asset_id,
                                std::vector<RouteImageDownloadItem> items,
                                std::string& out_error,
                                RouteImageTaskPresentation presentation)
{
    out_error.clear();
    const std::size_t total = items.size();
    if (!ensure_batch_mutex())
    {
        out_error = "Create image download mutex failed";
        return false;
    }
    if (!is_safe_asset_id(asset_id))
    {
        out_error = "Invalid route asset id";
        return false;
    }
    if (total == 0)
    {
        out_error = "No route images";
        return false;
    }

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (s_image_batch.status.busy ||
            s_image_batch.worker_task != nullptr ||
            s_image_batch.launch_pending)
        {
            if (s_image_batch.status.asset_id == asset_id)
            {
                promote_batch_presentation_locked(presentation);
                return true;
            }
            out_error = "Another route image download is running";
            return false;
        }
    }

    auto* ctx = new (std::nothrow) RouteImageBatchContext{};
    if (ctx == nullptr)
    {
        log_route_task_create_failed("route_img_dl_ctx",
                                     total,
                                     kRouteImageWorkerStackBytes);
        out_error = "Allocate image download task failed";
        return false;
    }
    ctx->kind = RouteImageBatchKind::Download;
    ctx->asset_id = asset_id;
    ctx->download_items = std::move(items);

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (s_image_batch.status.busy ||
            s_image_batch.worker_task != nullptr ||
            s_image_batch.launch_pending)
        {
            if (s_image_batch.status.asset_id == asset_id)
            {
                promote_batch_presentation_locked(presentation);
                delete ctx;
                return true;
            }
            out_error = "Another route image download is running";
            delete ctx;
            return false;
        }

        s_image_batch.launch_pending = true;
        s_image_batch.presentation = presentation;
        set_batch_status_locked(RouteImageDownloadPhase::Downloading,
                                true,
                                asset_id,
                                total,
                                0,
                                0,
                                0,
                                0,
                                0,
                                "Queued image download",
                                nullptr);
    }

    TaskHandle_t task_handle = nullptr;
    const BaseType_t task_ok = xTaskCreate(route_image_worker_task,
                                           "route_img_dl",
                                           kRouteImageWorkerStackBytes,
                                           ctx,
                                           kRouteImageWorkerPriority,
                                           &task_handle);

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (task_ok != pdPASS || task_handle == nullptr)
        {
            log_route_task_create_failed("route_img_dl",
                                         total,
                                         kRouteImageWorkerStackBytes);
            s_image_batch.worker_task = nullptr;
            s_image_batch.launch_pending = false;
            set_batch_status_locked(RouteImageDownloadPhase::Failed,
                                    false,
                                    asset_id,
                                    total,
                                    0,
                                    0,
                                    total,
                                    0,
                                    0,
                                    "Create image download task failed",
                                    "Create image download task failed");
            out_error = s_image_batch.status.message;
            delete ctx;
            return false;
        }
        if (s_image_batch.launch_pending)
        {
            s_image_batch.worker_task = task_handle;
            s_image_batch.launch_pending = false;
        }
    }
    return true;
}

bool start_route_image_cache_build(const std::string& asset_id,
                                   std::vector<RouteImageCacheItem> items,
                                   std::string& out_error,
                                   RouteImageTaskPresentation presentation,
                                   const std::string& package_route_path)
{
    out_error.clear();
    const auto total = items.size();
    if (!ensure_batch_mutex())
    {
        out_error = "Create image cache mutex failed";
        return false;
    }
    if (!is_safe_asset_id(asset_id))
    {
        out_error = "Invalid route asset id";
        return false;
    }
    if (items.empty() || items.size() > 64)
    {
        out_error = "No route image cache work";
        return false;
    }

    auto* ctx = new (std::nothrow) RouteImageBatchContext{};
    if (ctx == nullptr)
    {
        out_error = "Allocate image cache task failed";
        return false;
    }
    ctx->kind = RouteImageBatchKind::Cache;
    ctx->asset_id = asset_id;
    ctx->package_route_path = package_route_path;
    ctx->cache_items = std::move(items);

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (s_image_batch.status.busy ||
            s_image_batch.worker_task != nullptr ||
            s_image_batch.launch_pending)
        {
            if (s_image_batch.status.asset_id == asset_id)
            {
                promote_batch_presentation_locked(presentation);
                delete ctx;
                return true;
            }
            out_error = "Another route image task is running";
            delete ctx;
            return false;
        }

        s_image_batch.launch_pending = true;
        s_image_batch.presentation = presentation;
        set_batch_status_locked(RouteImageDownloadPhase::Caching,
                                true,
                                asset_id,
                                total,
                                0,
                                0,
                                0,
                                0,
                                0,
                                "Queued image cache",
                                nullptr);
    }

    TaskHandle_t task_handle = nullptr;
    const BaseType_t task_ok = xTaskCreate(route_image_worker_task,
                                           "route_img_cache",
                                           kRouteImageWorkerStackBytes,
                                           ctx,
                                           kRouteImageWorkerPriority,
                                           &task_handle);

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (task_ok != pdPASS || task_handle == nullptr)
        {
            s_image_batch.worker_task = nullptr;
            s_image_batch.launch_pending = false;
            set_batch_status_locked(RouteImageDownloadPhase::Failed,
                                    false,
                                    asset_id,
                                    items.size(),
                                    0,
                                    0,
                                    items.size(),
                                    0,
                                    0,
                                    "Create image cache task failed",
                                    "Create image cache task failed");
            out_error = s_image_batch.status.message;
            delete ctx;
            return false;
        }
        if (s_image_batch.launch_pending)
        {
            s_image_batch.worker_task = task_handle;
            s_image_batch.launch_pending = false;
        }
    }
    return true;
}

RouteImageDownloadStatus route_image_download_status()
{
    RouteImageDownloadStatus status{};
    if (!ensure_batch_mutex())
    {
        status.phase = RouteImageDownloadPhase::Failed;
        status.message = "Create image download mutex failed";
        status.error = status.message;
        return status;
    }
    BatchStateLock lock(s_image_batch.mutex);
    return s_image_batch.status;
}

} // namespace platform::ui::route_storage
