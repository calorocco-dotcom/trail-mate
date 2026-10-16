#include "hostlink/route_storage_lease.h"
#include "platform/esp/arduino_common/route_image_source_runtime.h"
#include "platform/ui/http_client_runtime.h"
#include "platform/ui/route_storage.h"
#include <cassert>
#include <memory>
#include <string>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
namespace image_http
{
inline bool ok = true, bad_stats = false, change_media = false;
inline unsigned calls = 0;
inline std::vector<uint8_t> body = {4, 5, 6, 7, 8};
} // namespace image_http
namespace platform::ui::device
{
bool sd_ready() { return image_fake::ready; }
} // namespace platform::ui::device
namespace platform::ui::http_client
{
bool download(const Request& request, WriteCallback callback, void* context, std::string& error, TransferStats* stats)
{
    ++image_http::calls;
    assert(request.max_bytes <= track_sharing::kImageSourceByteLimit);
    assert(request.buffer_size == 512 && request.tx_buffer_size == 512);
    if (image_http::change_media) ++image_fake::media;
    const bool written = callback(image_http::body.data(), 2, context) && callback(image_http::body.data() + 2, image_http::body.size() - 2, context);
    stats->http_status = 200;
    stats->bytes = uint32_t(image_http::body.size() + image_http::bad_stats);
    if (!written || !image_http::ok)
    {
        error = "HTTP failed";
        return false;
    }
    return true;
}
} // namespace platform::ui::http_client
namespace platform::ui::route_storage
{
constexpr size_t kRouteDownloadBufferSize = 512;
constexpr int kRouteDownloadTxBufferSize = 512;
constexpr size_t kRouteHttpInternalFreeTargetBytes = 18 * 1024, kRouteHttpInternalLargestTargetBytes = 8 * 1024;
bool route_image_url_supported(const std::string& url) { return url.substr(0, 8) == "https://"; }
bool ensure_parent_dir(const std::string&) { return true; }
inline void taskYIELD() {}
#include "image_download_actual.inc"
#include "image_query_public_actual.inc"
#include "image_query_reference_actual.inc"
} // namespace platform::ui::route_storage
int main()
{
    using Work = platform::esp::route_images::RouteImageSourceWork;
    static_assert(sizeof(Work) < 2048, "Image provenance workspace grew beyond its admission budget");
    const std::string path = "/routes/.trailmate/kml-12345678/images/img-0001.jpg", url = "https://example.invalid/photo.jpg";
    const auto reset = []()
    {image_fake::reset();image_fake::psram=true;image_fake::allocation_fail=false;image_http::ok=true;image_http::bad_stats=false;image_http::change_media=false; };
    reset();
    auto download = [&](const std::string& target = std::string{})
    { return platform::ui::route_storage::download_route_image_with_progress(url, target.empty() ? path : target, 4096, nullptr, nullptr); };
    const auto result = download();
    assert(result.ok && result.bytes == 5 && image_fake::live == 0);
    assert(image_fake::files.at(path) == image_http::body);
    assert(image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint8_t source_hash[32];
    {
        std::unique_ptr<Work> work(new (std::nothrow) Work);
        assert(work && work->referenceHash(url, source_hash) && work->matchesExisting(path, source_hash));
        source_hash[0] ^= 1;
        assert(!work->matchesExisting(path, source_hash));
        source_hash[0] ^= 1;
    }
    assert(image_fake::live == 0);
    assert(platform::ui::route_storage::route_image_source_matches(path, source_hash));
    assert(platform::ui::route_storage::route_image_source_matches_reference(path, url));
    assert(!platform::ui::route_storage::route_image_source_matches_reference(path, url + "?different=1"));
    image_fake::files[path] = {9, 9, 9};
    {
        std::unique_ptr<Work> work(new (std::nothrow) Work);
        assert(!work->matchesExisting(path, source_hash));
    }
    reset();
    image_fake::files[path] = {9, 9, 9};
    {
        std::unique_ptr<Work> work(new (std::nothrow) Work);
        assert(!work->matchesExisting(path, source_hash));
    }
    for (const auto& invalid : {"/routes/.trailmate/../images/img-0001.jpg", "/routes/.trailmate/a/images/img-0000.jpg", "/routes/.trailmate/a/images/img-0065.jpg", "/routes/.trailmate/a/images/img-0001.jpg/../other", "/elsewhere/img-0001.jpg"})
    {
        reset();
        const auto calls = image_http::calls;
        assert(!download(invalid).ok && image_http::calls == calls && image_fake::files.empty());
    }
    reset();
    image_fake::allocation_fail = true;
    const auto calls = image_http::calls;
    assert(!download().ok && image_http::calls == calls && image_fake::files.empty() && image_fake::last_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    reset();
    image_fake::psram = false;
    assert(download().ok && image_fake::last_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    reset();
    image_http::bad_stats = true;
    assert(!download().ok && !image_fake::files.count(path) && !image_fake::files.count(path + ".source"));
    reset();
    image_fake::flush_fail = true;
    assert(!download().ok && !image_fake::files.count(path));
    reset();
    image_fake::write_fail = true;
    assert(!download().ok && !image_fake::files.count(path));
    reset();
    image_http::ok = false;
    assert(!download().ok && !image_fake::files.count(path));
    reset();
    image_http::change_media = true;
    assert(!download().ok && !image_fake::files.count(path));
    reset();
    assert(hostlink::route_storage_lease::acquire());
    const auto busy_calls = image_http::calls;
    assert(!download().ok && image_http::calls == busy_calls && image_fake::files.empty());
    hostlink::route_storage_lease::release();
    reset();
    assert(download().ok);
    image_fake::files["/routes/.trailmate/kml-12345678/thumbs/thumb-0001.bmp"] = {9};
    image_fake::files["/routes/.trailmate/kml-12345678/views/view-0001.bmp"] = {10};
    image_http::body = {8, 7, 6, 5, 4};
    assert(download().ok);
    assert(!image_fake::files.count("/routes/.trailmate/kml-12345678/thumbs/thumb-0001.bmp") && !image_fake::files.count("/routes/.trailmate/kml-12345678/views/view-0001.bmp"));
    reset();
    image_fake::busy = true;
    {
        std::unique_ptr<Work> work(new (std::nothrow) Work);
        assert(!work->matchesExisting(path, source_hash) && image_fake::removes == 0 && image_fake::renames == 0);
    }
    assert(image_fake::live == 0);
    reset();
    image_fake::files[path] = std::vector<uint8_t>(75, 1);
    image_fake::open_fail = true;
    {
        platform::esp::route_images::SourceIo io;
        assert(io.open(path.c_str(), decltype(io)::Mode::Read).status == decltype(io)::OpenStatus::Unavailable);
        assert(io.open("/missing", decltype(io)::Mode::Read).status == decltype(io)::OpenStatus::Missing);
    }
}
