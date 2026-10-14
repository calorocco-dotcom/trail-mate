#pragma once
#include "track_sharing/kml_image_xml_stream.h"
#include "ui/support/lvgl_fs_utils.h"
#include <cstdlib>
#include <new>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui::support
{
struct RouteImageKmlWork
{
    char names[4096], source[4096], chunk[256];
    track_sharing::KmlImageXmlStream parser;
    explicit RouteImageKmlWork(track_sharing::KmlImageCallbacks callbacks)
        : parser(names, sizeof(names), source, sizeof(source), callbacks) {}
};
static_assert(sizeof(RouteImageKmlWork) <= 12544, "KML workspace is bounded and transient");

// No parser, name stack or source array on the ESP task stack. External RAM
// admission never falls back internally if the board has PSRAM.
inline bool readRouteImageKml(const char* path, track_sharing::KmlImageCallbacks callbacks)
{
    const auto normalized = fs::normalize_path(path);
    if (normalized.empty()) return false;
#if defined(ESP_PLATFORM)
    const auto caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    void* memory = heap_caps_malloc(sizeof(RouteImageKmlWork), caps | MALLOC_CAP_8BIT);
#else
    void* memory = std::malloc(sizeof(RouteImageKmlWork));
#endif
    if (!memory) return false;
    auto* work = new (memory) RouteImageKmlWork(callbacks);
    lv_fs_file_t file;
    bool ok = lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) == LV_FS_RES_OK;
    if (ok)
    {
        while (ok)
        {
            uint32_t count = 0;
            if (lv_fs_read(&file, work->chunk, sizeof(work->chunk), &count) != LV_FS_RES_OK || count > sizeof(work->chunk)) ok = false;
            else if (!count)
            {
                ok = work->parser.finish();
                break;
            }
            else ok = work->parser.feed(reinterpret_cast<const uint8_t*>(work->chunk), count);
        }
        if (lv_fs_close(&file) != LV_FS_RES_OK) ok = false;
    }
    work->~RouteImageKmlWork();
#if defined(ESP_PLATFORM)
    heap_caps_free(memory);
#else
    std::free(memory);
#endif
    return ok;
}
} // namespace ui::support
