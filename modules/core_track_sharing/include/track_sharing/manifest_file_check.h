#pragma once
#include "track_sharing/file_integrity.h"
#include "track_sharing/kml_xml_stream.h"
#include "track_sharing/upload_journal.h"
#include "track_sharing/verify_manifest.h"
namespace track_sharing
{
enum class ManifestFileResult : uint8_t
{
    Idle,
    Pending,
    Ready,
    Busy,
    Invalid,
    HashMismatch,
    StorageFailure,
    GeometryMismatch,
    Cancelled
};
// Single-use admitted job. Caller owns immutable file lease, source path, I/O,
// hash context, XML name stack and per-step scratch. Ready proves current
// implemented file/hash/index checks, not complete publication authorization.
template <class Io, class Hash>
class ManifestFileCheck
{
  public:
    ManifestFileCheck(Io& io, Hash& hash, void* names, size_t capacity) : integrity_(io, hash), xml_(names, capacity) {}
    ManifestFileResult begin(const char* path, const VerifiedManifestView& signed_view)
    {
        if (result_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!signed_view.verified || signed_view.manifest.file_hash.size != 32 || !signed_view.manifest.file_hash.data) return ManifestFileResult::Invalid;
        return beginFields(path, signed_view.manifest);
    }
    template <class PackageProof>
    ManifestFileResult beginVerifiedPackage(const char* path, const VerifiedManifestView& signed_view, const PackageProof& package)
    {
        if (result_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!signed_view.verified || signed_view.manifest.version != 2 || !package.matchesAttachments(signed_view.manifest)) return ManifestFileResult::Invalid;
        return beginFields(path, signed_view.manifest, true);
    }
    // Local publication preflight, not signature verification or authorization.
    // Platform supplies the current device public key and retains the immutable
    // raw manifest/file lease through signing, including revision reservation.
    ManifestFileResult beginCandidate(const char* path, geocaching::ByteView raw,
                                      geocaching::ByteView device_author, ManifestView& candidate)
    {
        if (result_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!device_author.data || device_author.size != 64 || !decodeManifest(raw, candidate)) return ManifestFileResult::Invalid;
        if (std::memcmp(candidate.author.data, device_author.data, 64)) return ManifestFileResult::Invalid;
        return beginFields(path, candidate);
    }
    // Current public key/hash/generation must come from the same authenticated
    // device identity read. This binds preflight to a sealed uploaded candidate,
    // not a manifest's self-declared file. Paths/leases remain platform-owned.
    ManifestFileResult beginUploadedCandidate(const char* path, geocaching::ByteView raw, geocaching::ByteView device_author,
                                              const UploadJournal& upload, uint32_t generation, geocaching::ByteView author_hash, ManifestView& candidate)
    {
        if (result_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!uploadJournalValid(upload) || upload.phase != UploadPhase::Sealed || generation != upload.generation ||
            !author_hash.data || author_hash.size != 32 || std::memcmp(author_hash.data, upload.author_hash, 32) ||
            !device_author.data || device_author.size != 64 || !decodeManifest(raw, candidate)) return ManifestFileResult::Invalid;
        if (std::memcmp(candidate.author.data, device_author.data, 64) || candidate.file_bytes != upload.total ||
            std::memcmp(candidate.file_hash.data, upload.file_hash, 32)) return ManifestFileResult::Invalid;
        return beginFields(path, candidate);
    }
    template <class PackageProof>
    ManifestFileResult beginUploadedPackageCandidate(const char* path, geocaching::ByteView raw, geocaching::ByteView device_author,
                                                     const UploadJournal& upload, uint32_t generation, geocaching::ByteView author_hash,
                                                     ManifestView& candidate, const PackageProof& package)
    {
        if (result_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!uploadJournalValid(upload) || upload.phase != UploadPhase::Sealed || generation != upload.generation ||
            !author_hash.data || author_hash.size != 32 || std::memcmp(author_hash.data, upload.author_hash, 32) ||
            !device_author.data || device_author.size != 64 || !decodeManifest(raw, candidate) || candidate.version != 2 ||
            std::memcmp(candidate.author.data, device_author.data, 64) || candidate.file_bytes != upload.total ||
            std::memcmp(candidate.file_hash.data, upload.file_hash, 32) || !package.matchesAttachments(candidate)) return ManifestFileResult::Invalid;
        return beginFields(path, candidate, true);
    }
    ManifestFileResult step(uint8_t* scratch, size_t capacity)
    {
        if (result_ != ManifestFileResult::Pending) return result_;
        const auto before = integrity_.checkedBytes();
        const auto status = integrity_.step(scratch, capacity);
        const auto count = integrity_.checkedBytes() - before;
        if (status == FileIntegrityResult::Pending)
        {
            if (count && !xml_.feed(scratch, count))
            {
                integrity_.cancel();
                return result_ = ManifestFileResult::Invalid;
            }
            return result_;
        }
        if (status == FileIntegrityResult::Ready)
        {
            GeometryExtent actual;
            if (!xml_.finish(actual)) return result_ = ManifestFileResult::Invalid;
            if (actual.west != expected_.west || actual.south != expected_.south || actual.east != expected_.east || actual.north != expected_.north || actual.points != expected_.points) return result_ = ManifestFileResult::GeometryMismatch;
            return result_ = ManifestFileResult::Ready;
        }
        return result_ = status == FileIntegrityResult::HashMismatch ? ManifestFileResult::HashMismatch : status == FileIntegrityResult::Invalid ? ManifestFileResult::Invalid
                                                                                                                                                 : ManifestFileResult::StorageFailure;
    }
    ManifestFileResult cancel()
    {
        if (result_ == ManifestFileResult::Pending)
        {
            integrity_.cancel();
            result_ = ManifestFileResult::Cancelled;
        }
        return result_;
    }
    uint32_t checkedBytes() const { return integrity_.checkedBytes(); }
    bool ready() const { return result_ == ManifestFileResult::Ready; }
    bool matchesCandidate(const ManifestView& manifest) const
    {
        return ready() && manifest.file_hash.size == 32 && integrity_.matchesExpected(manifest.file_bytes, manifest.file_hash.data) &&
               manifest.west == expected_.west && manifest.south == expected_.south && manifest.east == expected_.east && manifest.north == expected_.north && manifest.points == expected_.points;
    }

  private:
    ManifestFileResult beginFields(const char* path, const ManifestView& manifest, bool package_checked = false)
    {
        // Existing KML-only entry points still reject v2. Only the explicit
        // package entry point above can supply this admitted proof.
        if (manifest.version != 1 && !(manifest.version == 2 && package_checked)) return result_ = ManifestFileResult::Invalid;
        expected_ = {manifest.west, manifest.south, manifest.east, manifest.north, manifest.points};
        const auto started = integrity_.begin(path, manifest.file_bytes, manifest.file_hash.data);
        if (started != FileIntegrityResult::Pending) return result_ = started == FileIntegrityResult::StorageFailure ? ManifestFileResult::StorageFailure : ManifestFileResult::Invalid;
        return result_ = ManifestFileResult::Pending;
    }
    FileIntegrityCheck<Io, Hash> integrity_;
    KmlXmlStream xml_;
    GeometryExtent expected_;
    ManifestFileResult result_ = ManifestFileResult::Idle;
};
} // namespace track_sharing
