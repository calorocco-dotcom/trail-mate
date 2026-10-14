#pragma once
#include "track_sharing/attachment_index_file_check.h"

namespace track_sharing
{
enum class AttachmentResolveResult : uint8_t
{
    Ready,
    Missing,
    Invalid,
    StorageFailure
};
enum class AttachmentPackageResult : uint8_t
{
    Idle,
    Pending,
    Ready,
    Busy,
    Invalid,
    MissingRequired,
    HashMismatch,
    StorageFailure,
    Cancelled
};
// The platform lends immutable media/upload leases and two distinct hash
// contexts. Resolver maps an index ordinal to an authenticated sealed upload;
// references in KML never become filesystem paths. This job performs no writes.
// Ready proves index + available resources, not KML geometry/signature/install.
template <class Io, class Hash, class Resolver, size_t PathCapacity = 64>
class AttachmentPackageFileCheck
{
  public:
    AttachmentPackageFileCheck(Io& io, Hash& index_hash, Hash& resource_hash, Resolver& resolver,
                               uint8_t* window, size_t capacity, AttachmentFingerprint* fingerprints, size_t slots,
                               const AttachmentIndexBinding& binding, geocaching::ByteView kml_hash, uint32_t kml_bytes)
        : index_(io, index_hash, window, capacity, fingerprints, slots, binding, kml_hash, kml_bytes),
          resource_(io, resource_hash), resolver_(resolver)
    {
        const auto* self = reinterpret_cast<const uint8_t*>(this);
        if (&index_hash == &resource_hash ||
            signingSpansOverlap(window, capacity, self, sizeof(*this)) ||
            signingSpansOverlap(reinterpret_cast<const uint8_t*>(fingerprints), kMaxTrackAttachments * sizeof(*fingerprints), self, sizeof(*this)) ||
            signingSpansOverlap(binding.hash.data, binding.hash.size, self, sizeof(*this)) ||
            signingSpansOverlap(kml_hash.data, kml_hash.size, self, sizeof(*this))) result_ = AttachmentPackageResult::Invalid;
    }
    AttachmentPackageResult begin(const char* index_path)
    {
        if (result_ != AttachmentPackageResult::Idle) return result_ == AttachmentPackageResult::Invalid ? result_ : AttachmentPackageResult::Busy;
        if (index_.begin(index_path) != AttachmentIndexFileResult::Pending) return result_ = AttachmentPackageResult::Invalid;
        return result_ = AttachmentPackageResult::Pending;
    }
    AttachmentPackageResult step()
    {
        if (result_ != AttachmentPackageResult::Pending) return result_;
        if (resource_active_)
        {
            const auto checked = resource_.step(scratch_, sizeof(scratch_));
            if (checked == FileIntegrityResult::Pending) return result_;
            resource_active_ = false;
            if (checked != FileIntegrityResult::Ready)
                return stop(checked == FileIntegrityResult::HashMismatch ? AttachmentPackageResult::HashMismatch : checked == FileIntegrityResult::Invalid ? AttachmentPackageResult::Invalid
                                                                                                                                                           : AttachmentPackageResult::StorageFailure);
            available_ |= uint64_t(1) << ordinal_;
            return nextEntry();
        }
        const auto checked = index_.step();
        if (checked == AttachmentIndexFileResult::Pending) return result_;
        if (checked == AttachmentIndexFileResult::Ready) return result_ = AttachmentPackageResult::Ready;
        if (checked != AttachmentIndexFileResult::Entry)
            return stop(checked == AttachmentIndexFileResult::HashMismatch ? AttachmentPackageResult::HashMismatch : checked == AttachmentIndexFileResult::StorageFailure ? AttachmentPackageResult::StorageFailure
                                                                                                                                                                          : AttachmentPackageResult::Invalid);
        const auto* entry = index_.entry();
        if (!entry || ordinal_ >= kMaxTrackAttachments) return stop(AttachmentPackageResult::Invalid);
        if (entry->required) required_ |= uint64_t(1) << ordinal_;
        path_[0] = 0;
        const auto resolved = resolver_.resolve(ordinal_, *entry, path_, sizeof(path_));
        if (resolved == AttachmentResolveResult::Missing)
            return entry->required ? stop(AttachmentPackageResult::MissingRequired) : nextEntry();
        if (resolved != AttachmentResolveResult::Ready)
            return stop(resolved == AttachmentResolveResult::StorageFailure ? AttachmentPackageResult::StorageFailure : AttachmentPackageResult::Invalid);
        if (resource_.begin(path_, entry->bytes, entry->hash.data) != FileIntegrityResult::Pending)
            return stop(AttachmentPackageResult::Invalid);
        resource_active_ = true;
        return result_;
    }
    AttachmentPackageResult cancel()
    {
        return result_ == AttachmentPackageResult::Pending ? stop(AttachmentPackageResult::Cancelled) : result_;
    }
    bool ready() const { return result_ == AttachmentPackageResult::Ready && index_.ready() && (available_ & required_) == required_; }
    bool matchesIndex(const char* path, const AttachmentIndexBinding& binding) const { return ready() && index_.matchesSource(path, binding); }
    bool allAvailable() const { return ready() && (ordinal_ == 64 ? available_ == UINT64_MAX : available_ == ((uint64_t(1) << ordinal_) - 1)); }
    uint64_t availability() const { return ready() ? available_ : 0; }
    uint32_t packageBytes() const { return ready() ? index_.packageBytes() : 0; }

  private:
    AttachmentPackageResult nextEntry()
    {
        if (!index_.advance()) return stop(AttachmentPackageResult::Invalid);
        ++ordinal_;
        return result_;
    }
    AttachmentPackageResult stop(AttachmentPackageResult result)
    {
        index_.cancel();
        resource_.cancel();
        resource_active_ = false;
        return result_ = result;
    }
    AttachmentIndexFileCheck<Io, Hash> index_;
    FileIntegrityCheck<Io, Hash> resource_;
    Resolver& resolver_;
    char path_[PathCapacity]{};
    uint8_t scratch_[320]{};
    uint64_t available_ = 0, required_ = 0;
    uint8_t ordinal_ = 0;
    AttachmentPackageResult result_ = AttachmentPackageResult::Idle;
    bool resource_active_ = false;
};
} // namespace track_sharing
