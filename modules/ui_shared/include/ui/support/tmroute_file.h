#pragma once
#include "platform/ui/navigation_trace.h"
#include "tmap/memory.h"
#include "tmap/route_archive.h"
#include "ui/support/lvgl_fs_utils.h"
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui::support
{
class RouteUiMemory final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override
    {
#if defined(ESP_PLATFORM)
        return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) != 0;
#else
        return true; // Host acceptance adapter; not physical PSRAM.
#endif
    }
    void* allocate(size_t bytes, size_t alignment, tmap::MemoryRegion region) override
    {
        if (region != tmap::MemoryRegion::External || !hasExternalMemory()) return nullptr;
#if defined(ESP_PLATFORM)
        return heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        (void)alignment;
        return std::malloc(bytes);
#endif
    }
    void release(void* data, size_t, size_t, tmap::MemoryRegion) override
    {
#if defined(ESP_PLATFORM)
        heap_caps_free(data);
#else
        std::free(data);
#endif
    }
    template <class T, class... Args>
    static T* create(Args&&... args)
    {
        RouteUiMemory memory;
        void* data = memory.allocate(sizeof(T), alignof(T), tmap::MemoryRegion::External);
        return data ? new (data) T(std::forward<Args>(args)...) : nullptr;
    }
    template <class T>
    static void destroy(T*& data)
    {
        if (!data) return;
        data->~T();
        RouteUiMemory memory;
        memory.release(data, sizeof(T), alignof(T), tmap::MemoryRegion::External);
        data = nullptr;
    }
};

// Native route files have an explicit 8 MiB producer limit. LVGL's 32-bit seek
// API is sufficient here; map containers continue to use the 64-bit map adapter.
class LvglRouteFile final : public tmap::RandomAccessFile
{
  public:
    ~LvglRouteFile() override { close(); }
    tmap::Result begin(const char* path)
    {
        close();
        const auto normalized = fs::normalize_path(path);
        if (normalized.empty() || normalized.size() >= sizeof(path_)) return tmap::Result::Invalid;
        std::memcpy(path_, normalized.c_str(), normalized.size() + 1);
        stage_ = 1;
        return tmap::Result::More;
    }
    tmap::Result openStep()
    {
        lv_fs_res_t status = LV_FS_RES_INV_PARAM;
        if (stage_ == 1)
        {
            status = lv_fs_open(&file_, path_, LV_FS_MODE_RD);
            if (status == LV_FS_RES_OK)
            {
                opened_ = true;
                stage_ = 2;
            }
        }
        else if (stage_ == 2)
        {
            status = lv_fs_seek(&file_, 0, LV_FS_SEEK_END);
            if (status == LV_FS_RES_OK) stage_ = 3;
        }
        else if (stage_ == 3)
        {
            status = lv_fs_tell(&file_, &length_);
            if (status == LV_FS_RES_OK)
            {
                if (!length_ || length_ > 8 * 1024 * 1024) return tmap::Result::Unsupported;
                stage_ = 4;
            }
        }
        else if (stage_ == 4)
        {
            status = lv_fs_seek(&file_, 0, LV_FS_SEEK_SET);
            if (status == LV_FS_RES_OK)
            {
                stage_ = 5;
                return tmap::Result::Ok;
            }
        }
        else return stage_ == 5 ? tmap::Result::Ok : tmap::Result::Invalid;
        const auto converted = convert(status);
        if (converted != tmap::Result::Ok && converted != tmap::Result::Busy)
            platform::ui::navigation::trace("[Navigation][reader_file] operation=open stage=%u lvgl=%u result=%s\n",
                                            unsigned(stage_), unsigned(status), tmap::resultName(converted));
        return converted == tmap::Result::Ok ? tmap::Result::More : converted;
    }
    bool waitingForOpen() const { return stage_ == 1; }
    uint64_t size() const override { return length_; }
    tmap::Status readAt(uint64_t offset, uint8_t* data, size_t count) override
    {
        if (stage_ != 5 || offset > length_ || count > length_ - offset) return tmap::Status::Invalid;
        auto result = convert(lv_fs_seek(&file_, static_cast<uint32_t>(offset), LV_FS_SEEK_SET));
        if (result == tmap::Result::Busy) return tmap::Status::Busy;
        if (result != tmap::Result::Ok)
        {
            platform::ui::navigation::trace("[Navigation][reader_file] operation=seek offset=%lu result=%s\n",
                                            static_cast<unsigned long>(offset), tmap::resultName(result));
            return tmap::Status::IoError;
        }
        uint32_t read = 0;
        result = convert(lv_fs_read(&file_, data, static_cast<uint32_t>(count), &read));
        if (result == tmap::Result::Busy) return tmap::Status::Busy;
        if (result == tmap::Result::Ok && read == count) return tmap::Status::Ok;
        platform::ui::navigation::trace("[Navigation][reader_file] operation=read offset=%lu requested=%u read=%lu result=%s\n",
                                        static_cast<unsigned long>(offset), static_cast<unsigned>(count), static_cast<unsigned long>(read), tmap::resultName(result));
        return tmap::Status::IoError;
    }
    void close()
    {
        if (opened_) lv_fs_close(&file_);
        opened_ = false;
        stage_ = 0;
        length_ = 0;
    }

  private:
    static tmap::Result convert(lv_fs_res_t result)
    {
        if (result == LV_FS_RES_OK) return tmap::Result::Ok;
        if (result == LV_FS_RES_BUSY || result == LV_FS_RES_LOCKED || result == LV_FS_RES_TOUT) return tmap::Result::Busy;
        return result == LV_FS_RES_NOT_EX ? tmap::Result::Missing : tmap::Result::IoError;
    }
    lv_fs_file_t file_{};
    char path_[192]{};
    uint32_t length_ = 0;
    uint8_t stage_ = 0;
    bool opened_ = false;
};
} // namespace ui::support
