#pragma once
#include "platform/ui/route_storage.h"
namespace ui::support
{
// Both route screens use the same rule. A legacy file's presence is useful for
// compatibility guidance but is not a source match. No URL is retained here.
template <class Image>
void refreshRouteImageSourceState(Image& image, bool include_cache)
{
    image.downloaded = image.source_hash_valid && platform::ui::route_storage::route_image_source_matches(image.local_path, image.source_hash);
    image.preview_ready = include_cache && image.downloaded && platform::ui::route_storage::route_asset_file_exists(image.preview_path);
    image.view_ready = include_cache && image.downloaded && platform::ui::route_storage::route_asset_file_exists(image.view_path);
}
template <class Images>
bool routeImagesNeedSourceReview(const Images& images)
{
    for (const auto& image : images)
        if (!image.downloaded && platform::ui::route_storage::route_asset_file_exists(image.local_path)) return true;
    return false;
}
} // namespace ui::support
