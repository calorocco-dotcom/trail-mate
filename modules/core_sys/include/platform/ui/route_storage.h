#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace platform::ui::route_storage
{

struct RouteImageDownloadResult
{
    bool ok = false;
    std::uint32_t bytes = 0;
    int http_status = 0;
    std::string error{};
};

struct RouteImageDownloadItem
{
    std::string url{};
    std::string output_path{};
    std::string preview_path{};
    std::string view_path{};
};

struct RouteImageCacheItem
{
    std::string source_path{};
    std::string preview_path{};
    std::string view_path{};
    uint8_t source_hash[32]{};
    bool source_hash_valid = false;
};

enum class RouteImageDownloadPhase : uint8_t
{
    Idle = 0,
    Downloading,
    Caching,
    Done,
    Failed,
};

enum class RouteImageTaskPresentation : uint8_t
{
    Hidden = 0,
    PageOnly,
    UserVisible,
};

struct RouteImageDownloadStatus
{
    RouteImageDownloadPhase phase = RouteImageDownloadPhase::Idle;
    bool busy = false;
    RouteImageTaskPresentation presentation = RouteImageTaskPresentation::Hidden;
    std::string asset_id{};
    std::string message{};
    std::string error{};
    std::size_t total = 0;
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::size_t current_index = 0;
    std::uint32_t bytes = 0;
    std::uint32_t current_bytes = 0;
    std::uint32_t current_total_bytes = 0;
};

bool is_supported();
bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count = 64);
bool remove_route(const std::string& path);
const char* route_dir();
bool ensure_route_asset_dir(const std::string& asset_id, std::string& out_dir);
bool count_route_saved_images(const std::string& asset_id,
                              std::size_t max_count,
                              std::size_t& out_count);
bool route_asset_file_exists(const std::string& path);
bool route_image_reference_hash(std::string_view reference, uint8_t output[32]);
// Fast source-binding and length check. A true result is not an author's
// signature or an independent full-file hash verification.
bool route_image_source_matches(const std::string& path, const uint8_t expected_source_hash[32]);
RouteImageDownloadResult download_route_image(const std::string& url,
                                              const std::string& output_path,
                                              std::uint32_t max_bytes = 4U * 1024U * 1024U);
bool start_route_image_download(const std::string& asset_id,
                                std::vector<RouteImageDownloadItem> items,
                                std::string& out_error,
                                RouteImageTaskPresentation presentation =
                                    RouteImageTaskPresentation::UserVisible);
bool start_route_image_cache_build(const std::string& asset_id,
                                   std::vector<RouteImageCacheItem> items,
                                   std::string& out_error,
                                   RouteImageTaskPresentation presentation =
                                       RouteImageTaskPresentation::PageOnly,
                                   const std::string& package_route_path = {});
RouteImageDownloadStatus route_image_download_status();

} // namespace platform::ui::route_storage
