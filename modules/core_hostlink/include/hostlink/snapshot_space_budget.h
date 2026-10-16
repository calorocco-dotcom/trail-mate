#pragma once
#include "hostlink/track_snapshot.h"
namespace hostlink::management
{
// Constant-memory accounting includes orphaned/unknown private files. An
// incomplete directory traversal never authorizes another allocation.
class SnapshotSpaceBudget
{
  public:
    explicit SnapshotSpaceBudget(uint32_t source_bytes)
        : remaining_(kBytes - uint64_t(source_bytes) - 64), valid_(source_bytes && source_bytes <= kTrackSnapshotMaxBytes) {}
    bool add(uint64_t bytes, bool directory)
    {
        if (!valid_ || directory || ++files_ >= kFiles || bytes > remaining_) return valid_ = false;
        remaining_ -= bytes;
        return true;
    }
    bool complete() const { return valid_; }
    static constexpr uint64_t kBytes = 2ULL * (kTrackSnapshotMaxBytes + 64ULL);
    static constexpr unsigned kFiles = 8;

  private:
    uint64_t remaining_;
    unsigned files_ = 0;
    bool valid_;
};
static_assert(sizeof(SnapshotSpaceBudget) <= 16, "Snapshot quota must not hold a file list");
} // namespace hostlink::management
