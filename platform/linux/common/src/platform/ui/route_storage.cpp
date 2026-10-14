#include "platform/ui/route_storage.h"
#include "track_sharing/image_source_binding.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

#include "platform/linux/runtime_paths.h"

namespace platform::ui::route_storage
{
namespace
{

std::string s_route_dir_cache{};
std::mutex s_image_batch_mutex;
RouteImageDownloadStatus s_image_batch_status{};
bool s_image_batch_running = false;
RouteImageTaskPresentation s_image_batch_presentation =
    RouteImageTaskPresentation::Hidden;

std::filesystem::path route_dir_path()
{
    return ::platform::linux_runtime::resolve_paths().sd_root / "routes";
}

bool ensure_route_dir()
{
    std::error_code ec;
    const std::filesystem::path dir = route_dir_path();
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        return false;
    }
    s_route_dir_cache = dir.string();
    return true;
}

bool is_safe_asset_id(const std::string& asset_id)
{
    if (asset_id.empty() || asset_id.size() > 48)
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

bool parse_route_image_name(const std::string& name,
                            std::size_t max_count,
                            std::size_t& out_index)
{
    out_index = 0;
    if (max_count == 0 || name.size() != 12 || name.rfind("img-", 0) != 0)
    {
        return false;
    }
    std::uint32_t value = 0;
    for (std::size_t offset = 4; offset < 8; ++offset)
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
        value == 0 ||
        value > max_count)
    {
        return false;
    }
    out_index = static_cast<std::size_t>(value - 1U);
    return true;
}

std::filesystem::path resolve_route_path(const std::string& path_or_name)
{
    std::filesystem::path candidate(path_or_name);
    if (candidate.is_absolute())
    {
        return candidate;
    }
    return route_dir_path() / candidate;
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
                             const char* error)
{
    s_image_batch_status.phase = phase;
    s_image_batch_status.busy = busy;
    s_image_batch_status.presentation = s_image_batch_presentation;
    s_image_batch_status.asset_id = asset_id;
    s_image_batch_status.total = total;
    s_image_batch_status.processed = processed;
    s_image_batch_status.saved = saved;
    s_image_batch_status.failed = failed;
    s_image_batch_status.current_index = current_index;
    s_image_batch_status.bytes = bytes;
    s_image_batch_status.current_bytes = 0;
    s_image_batch_status.current_total_bytes = 0;
    s_image_batch_status.message = message ? message : "";
    s_image_batch_status.error = error ? error : "";
}

void promote_batch_presentation_locked(RouteImageTaskPresentation presentation)
{
    if (presentation == RouteImageTaskPresentation::UserVisible)
    {
        s_image_batch_presentation = presentation;
        s_image_batch_status.presentation = presentation;
    }
}

void route_image_worker(std::string asset_id, std::vector<RouteImageDownloadItem> items)
{
    const std::size_t total = items.size();
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::uint32_t bytes = 0;
    char detail[96]{};

    for (std::size_t index = 0; index < total; ++index)
    {
        const auto& item = items[index];
        {
            std::lock_guard<std::mutex> lock(s_image_batch_mutex);
            std::snprintf(detail,
                          sizeof(detail),
                          "Image %u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            set_batch_status_locked(RouteImageDownloadPhase::Downloading,
                                    true,
                                    asset_id,
                                    total,
                                    processed,
                                    saved,
                                    failed,
                                    index,
                                    bytes,
                                    detail,
                                    nullptr);
        }

        if (!item.output_path.empty() && route_asset_file_exists(item.output_path))
        {
            ++saved;
        }
        else
        {
            const auto result = download_route_image(item.url, item.output_path);
            if (result.ok)
            {
                ++saved;
                bytes += result.bytes;
            }
            else
            {
                ++failed;
            }
        }
        ++processed;
    }

    const bool ok = failed == 0 && saved >= total;
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
    {
        std::lock_guard<std::mutex> lock(s_image_batch_mutex);
        set_batch_status_locked(ok ? RouteImageDownloadPhase::Done : RouteImageDownloadPhase::Failed,
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
        s_image_batch_running = false;
    }
}

} // namespace

bool is_supported()
{
    return ensure_route_dir();
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!ensure_route_dir())
    {
        return false;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(route_dir_path(), ec), end;
         !ec && it != end; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (!it->is_regular_file(ec) || ec)
        {
            continue;
        }
        out_routes.push_back(it->path().filename().string());
    }

    std::sort(out_routes.begin(), out_routes.end());
    if (out_routes.size() > max_count)
    {
        out_routes.resize(max_count);
    }
    return true;
}

bool remove_route(const std::string& path)
{
    // Prevent absolute paths or traversal — only operate under the routes dir.
    std::filesystem::path resolved;
    if (!::platform::linux_runtime::resolve_child_under_root(
            route_dir_path(), path, resolved))
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::remove(resolved, ec) && !ec;
}

const char* route_dir()
{
    if (s_route_dir_cache.empty())
    {
        (void)ensure_route_dir();
    }
    return s_route_dir_cache.c_str();
}

bool ensure_route_asset_dir(const std::string& asset_id, std::string& out_dir)
{
    out_dir.clear();
    if (!ensure_route_dir() || !is_safe_asset_id(asset_id))
    {
        return false;
    }

    const std::filesystem::path dir = route_dir_path() / ".trailmate" / asset_id;
    std::error_code ec;
    std::filesystem::create_directories(dir / "images", ec);
    if (!ec)
    {
        std::filesystem::create_directories(dir / "thumbs", ec);
    }
    if (!ec)
    {
        std::filesystem::create_directories(dir / "views", ec);
    }
    if (ec)
    {
        return false;
    }
    out_dir = dir.string();
    return true;
}

bool count_route_saved_images(const std::string& asset_id,
                              std::size_t max_count,
                              std::size_t& out_count)
{
    out_count = 0;
    if (!ensure_route_dir() || !is_safe_asset_id(asset_id) || max_count == 0)
    {
        return false;
    }

    const std::filesystem::path image_dir =
        route_dir_path() / ".trailmate" / asset_id / "images";
    std::error_code ec;
    if (!std::filesystem::is_directory(image_dir, ec) || ec)
    {
        return false;
    }

    std::vector<std::uint8_t> seen(max_count, 0);
    for (std::filesystem::directory_iterator it(image_dir, ec), end;
         !ec && it != end && out_count < max_count; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (!it->is_regular_file(ec) || ec)
        {
            continue;
        }
        std::size_t index = 0;
        if (parse_route_image_name(it->path().filename().string(), max_count, index) &&
            index < seen.size() &&
            !seen[index])
        {
            seen[index] = 1;
            ++out_count;
        }
    }
    return !ec;
}

bool route_asset_file_exists(const std::string& path)
{
    std::error_code ec;
    return !path.empty() &&
           std::filesystem::is_regular_file(std::filesystem::path(path), ec) &&
           !ec;
}

bool route_image_source_matches(const std::string& path, const uint8_t expected_source_hash[32])
{
    if (path.empty() || !expected_source_hash) return false;
    const auto root = route_dir_path();
    const auto input = std::filesystem::path(path);
    const auto relative = input.is_absolute() ? input.lexically_relative(root) : input;
    std::filesystem::path resolved;
    if (!::platform::linux_runtime::resolve_child_under_root(root, relative.generic_string(), resolved)) return false;
    std::error_code error;
    std::filesystem::path binding;
    const auto binding_relative = relative.generic_string() + ".source";
    if (!::platform::linux_runtime::resolve_child_under_root(root, binding_relative, binding)) return false;
    if (std::filesystem::file_size(binding, error) != track_sharing::kImageSourceBindingBytes || error) return false;
    uint8_t bytes[track_sharing::kImageSourceBindingBytes];
    std::ifstream file(binding, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(bytes), sizeof(bytes))) return false;
    track_sharing::ImageSourceBindingView record;
    if (!track_sharing::decodeImageSourceBinding(bytes, sizeof(bytes), record) || std::memcmp(record.source_hash.data, expected_source_hash, 32)) return false;
    return std::filesystem::file_size(resolved, error) == record.file_bytes && !error;
}

RouteImageDownloadResult download_route_image(const std::string& url,
                                              const std::string& output_path,
                                              std::uint32_t max_bytes)
{
    (void)url;
    (void)output_path;
    (void)max_bytes;
    RouteImageDownloadResult result{};
    result.error = "Route image download unsupported on host";
    return result;
}

bool route_image_reference_hash(std::string_view reference, uint8_t output[32])
{
    if (!output || reference.empty() || reference.size() > 4096) return false;
#if defined(TRAIL_MATE_HAS_OPENSSL)
    unsigned length = 0;
    return EVP_Digest(reference.data(), reference.size(), output, &length, EVP_sha256(), nullptr) == 1 && length == 32;
#else
    return false;
#endif
}

bool start_route_image_download(const std::string& asset_id,
                                std::vector<RouteImageDownloadItem> items,
                                std::string& out_error,
                                RouteImageTaskPresentation presentation)
{
    out_error.clear();
    if (!is_safe_asset_id(asset_id))
    {
        out_error = "Invalid route asset id";
        return false;
    }
    if (items.empty())
    {
        out_error = "No route images";
        return false;
    }
    const std::size_t total = items.size();

    {
        std::lock_guard<std::mutex> lock(s_image_batch_mutex);
        if (s_image_batch_running || s_image_batch_status.busy)
        {
            if (s_image_batch_status.asset_id == asset_id)
            {
                promote_batch_presentation_locked(presentation);
                return true;
            }
            out_error = "Another route image download is running";
            return false;
        }
        s_image_batch_running = true;
        s_image_batch_presentation = presentation;
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

    try
    {
        std::thread(route_image_worker, asset_id, std::move(items)).detach();
        return true;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(s_image_batch_mutex);
        s_image_batch_running = false;
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
        out_error = s_image_batch_status.message;
        return false;
    }
}

bool start_route_image_cache_build(const std::string& asset_id,
                                   std::vector<RouteImageCacheItem> items,
                                   std::string& out_error,
                                   RouteImageTaskPresentation presentation,
                                   const std::string& package_route_path)
{
    (void)asset_id;
    (void)items;
    (void)presentation;
    (void)package_route_path;
    out_error = "Route image cache unsupported on host";
    return false;
}

RouteImageDownloadStatus route_image_download_status()
{
    std::lock_guard<std::mutex> lock(s_image_batch_mutex);
    return s_image_batch_status;
}

} // namespace platform::ui::route_storage
