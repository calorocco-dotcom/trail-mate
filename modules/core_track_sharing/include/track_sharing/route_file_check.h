#pragma once
#include "track_sharing/file_integrity.h"
#include "track_sharing/kml_xml_stream.h"
namespace track_sharing
{
// Single-use validation job. The platform must retain an immutable source/SD
// lease and current identity binding through the eventual atomic install.
// On ESP this object belongs to an admitted PSRAM owner, never a task stack.
// Caller lends the XML name stack, hash context and shared per-step scratch.
template <class Io, class Hash>
class RouteFileCheck
{
  public:
    RouteFileCheck(Io& io, Hash& hash, void* names, size_t capacity)
        : integrity_(io, hash), xml_(names, capacity) {}
    FileIntegrityResult begin(const char* path, uint32_t total, const uint8_t hash[32])
    {
        if (result_ != FileIntegrityResult::Idle) return FileIntegrityResult::Busy;
        result_ = integrity_.begin(path, total, hash);
        if (result_ == FileIntegrityResult::Pending) path_ = path;
        return result_;
    }
    FileIntegrityResult step(uint8_t* scratch, size_t capacity)
    {
        if (result_ != FileIntegrityResult::Pending) return result_;
        const auto before = integrity_.checkedBytes();
        const auto status = integrity_.step(scratch, capacity);
        const auto count = integrity_.checkedBytes() - before;
        if (status == FileIntegrityResult::Pending)
        {
            if (count && !xml_.feed(scratch, count))
            {
                integrity_.cancel();
                return result_ = FileIntegrityResult::Invalid;
            }
            return result_;
        }
        if (status == FileIntegrityResult::Ready && !xml_.finish(extent_))
            return result_ = FileIntegrityResult::Invalid;
        return result_ = status;
    }
    FileIntegrityResult cancel()
    {
        if (result_ == FileIntegrityResult::Pending)
        {
            integrity_.cancel();
            result_ = FileIntegrityResult::Cancelled;
        }
        return result_;
    }
    bool ready() const { return result_ == FileIntegrityResult::Ready; }
    bool matchesSource(const char* path, uint32_t bytes, const uint8_t* hash) const
    {
        return ready() && path && path_ && !std::strcmp(path, path_) && integrity_.matchesExpected(bytes, hash);
    }
    uint32_t checkedBytes() const { return integrity_.checkedBytes(); }
    const GeometryExtent* geometry() const { return ready() ? &extent_ : nullptr; }

  private:
    FileIntegrityCheck<Io, Hash> integrity_;
    KmlXmlStream xml_;
    GeometryExtent extent_{};
    const char* path_ = nullptr;
    FileIntegrityResult result_ = FileIntegrityResult::Idle;
};
} // namespace track_sharing
