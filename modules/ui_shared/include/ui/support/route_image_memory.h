#pragma once
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif
namespace ui::support
{
// Optional, bounded image storage. Failed admission returns false, never
// throws/aborts or falls back internally when external RAM is present.
template <class T, size_t Capacity = 64>
class RouteImageVector
{
  public:
    RouteImageVector() = default;
    ~RouteImageVector() { clear(); }
    RouteImageVector(const RouteImageVector&) = delete;
    RouteImageVector& operator=(const RouteImageVector&) = delete;
    RouteImageVector(RouteImageVector&& other) noexcept { swap(other); }
    RouteImageVector& operator=(RouteImageVector&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            swap(other);
        }
        return *this;
    }
    bool reserve(size_t wanted)
    {
        if (wanted > Capacity) return false;
        if (!wanted || data_) return true;
        return allocate();
    }
    bool push_back(T&& value)
    {
        static_assert(sizeof(T) <= 256, "Image object exceeds admission budget");
        static_assert(Capacity > 0 && Capacity <= 255, "Route buffer has a fixed byte-sized count");
        if (size_ == Capacity) return false;
        if (!data_ && !allocate()) return false;
        new (data_ + size_) T(std::move(value));
        ++size_;
        return true;
    }
    bool push_back(const T& value)
    {
        T copy(value);
        return push_back(std::move(copy));
    }
    void resize(size_t wanted)
    {
        while (size_ > wanted) data_[--size_].~T(); // bounded buffer only shrinks
    }
    T& front() { return data_[0]; }
    const T& front() const { return data_[0]; }
    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }

  private:
    bool allocate()
    {
#if defined(ESP_PLATFORM)
        const auto caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
        data_ = static_cast<T*>(heap_caps_malloc(Capacity * sizeof(T), caps | MALLOC_CAP_8BIT));
#else
        data_ = static_cast<T*>(std::malloc(Capacity * sizeof(T)));
#endif
        if (!data_)
        {
            failed_ = true;
            return false;
        }
        return true;
    }

  public:
    void clear() noexcept
    {
        while (size_) data_[--size_].~T();
#if defined(ESP_PLATFORM)
        heap_caps_free(data_);
#else
        std::free(data_);
#endif
        data_ = nullptr;
        failed_ = false;
    }
    void swap(RouteImageVector& other) noexcept
    {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(failed_, other.failed_);
    }
    size_t size() const { return size_; }
    bool empty() const { return !size_; }
    bool failed() const { return failed_; }
    T* data() { return data_; }
    const T* data() const { return data_; }
    T* begin() { return data_; }
    const T* begin() const { return data_; }
    T* end() { return data_ ? data_ + size_ : nullptr; }
    const T* end() const { return data_ ? data_ + size_ : nullptr; }
    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

  private:
    T* data_ = nullptr;
    uint8_t size_ = 0;
    bool failed_ = false;
};
} // namespace ui::support
