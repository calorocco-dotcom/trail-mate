#pragma once
#include "track_sharing/manifest_file_check.h"
#include "track_sharing/uploaded_manifest_reader.h"
namespace track_sharing
{
// Public shared package: upload journals belong to the current local USB actor,
// while the manifest signature belongs to its original (possibly remote)
// author. Never re-sign or substitute the local author. Caller owns immutable
// leases, buffers, paths, expected object/revision, metadata and hash context.
template <class Io, class Hash, class Crypto>
class SignedPackageFileCheck
{
  public:
    SignedPackageFileCheck(Io& io, Hash& hash, Crypto& crypto, const UploadJournal& signed_upload, const UploadJournal& kml_upload,
                           const uint32_t& generation, const uint8_t* actor_hash, void* names, size_t names_capacity,
                           VerifiedManifestView& verified, uint8_t* workspace, size_t workspace_capacity)
        : reader_(io, hash, signed_upload, generation, actor_hash), file_(io, hash, names, names_capacity),
          hash_(hash), crypto_(crypto), kml_upload_(kml_upload), generation_(generation), actor_hash_(actor_hash),
          verified_(verified), workspace_(workspace), workspace_capacity_(workspace_capacity) {}
    ManifestFileResult begin(const char* signed_path, const char* kml_path, geocaching::ByteView expected_object, geocaching::ByteView expected_revision,
                             uint8_t* raw, size_t capacity)
    {
        if (result_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!kml_path || !expected_object.data || expected_object.size != 32 || !expected_revision.data || expected_revision.size != 32 || !workspace_ || workspace_capacity_ < 4200 ||
            !uploadJournalValid(kml_upload_) || kml_upload_.phase != UploadPhase::Sealed || !identityMatches()) return result_ = ManifestFileResult::Invalid;
        if (signingSpansOverlap(expected_object.data, 32, raw, capacity) || signingSpansOverlap(expected_revision.data, 32, raw, capacity) ||
            signingSpansOverlap(expected_object.data, 32, workspace_, workspace_capacity_) || signingSpansOverlap(expected_revision.data, 32, workspace_, workspace_capacity_)) return result_ = ManifestFileResult::Invalid;
        expected_object_ = expected_object;
        expected_revision_ = expected_revision;
        kml_path_ = kml_path;
        const auto read = reader_.begin(signed_path, raw, capacity);
        return result_ = read == FileIntegrityResult::Pending ? ManifestFileResult::Pending : map(read);
    }
    template <class Package>
    ManifestFileResult step(Package& package, uint8_t* scratch, size_t capacity)
    {
        if (result_ != ManifestFileResult::Pending) return result_;
        if (!identityMatches())
        {
            cancel();
            return result_ = ManifestFileResult::Invalid;
        }
        if (checking_kml_) return result_ = file_.step(scratch, capacity);
        const auto read = reader_.step(scratch, capacity);
        if (read == FileIntegrityResult::Pending) return result_;
        if (read != FileIntegrityResult::Ready) return result_ = map(read);
        if (!authenticated_)
        {
            if (verifyManifest(reader_.bytes(), crypto_, hash_, workspace_, workspace_capacity_, verified_) != ManifestVerification::Valid ||
                std::memcmp(verified_.object_id, expected_object_.data, 32) || std::memcmp(verified_.revision_hash, expected_revision_.data, 32) ||
                verified_.manifest.file_bytes != kml_upload_.total || std::memcmp(verified_.manifest.file_hash.data, kml_upload_.file_hash, 32)) return result_ = ManifestFileResult::Invalid;
            authenticated_ = true;
        }
        if (verified_.manifest.version == 2)
        {
            const auto checked = package.checkAttachments(verified_.manifest, scratch, capacity);
            if (checked == ManifestFileResult::Pending) return result_;
            if (checked != ManifestFileResult::Ready) return result_ = checked;
            result_ = file_.beginVerifiedPackage(kml_path_, verified_, package);
        }
        else result_ = package.matchesAttachments(verified_.manifest) ? file_.begin(kml_path_, verified_) : ManifestFileResult::Invalid;
        checking_kml_ = result_ == ManifestFileResult::Pending;
        return result_;
    }
    ManifestFileResult cancel()
    {
        reader_.cancel();
        file_.cancel();
        if (result_ == ManifestFileResult::Pending) result_ = ManifestFileResult::Cancelled;
        return result_;
    }
    bool ready() const { return result_ == ManifestFileResult::Ready && authenticated_ && identityMatches() && file_.ready(); }
    geocaching::ByteView signedBytes() const { return ready() ? reader_.bytes() : geocaching::ByteView{}; }

  private:
    bool identityMatches() const { return actor_hash_ && generation_ && generation_ == kml_upload_.generation && !std::memcmp(actor_hash_, kml_upload_.author_hash, 32); }
    static ManifestFileResult map(FileIntegrityResult result)
    {
        return result == FileIntegrityResult::HashMismatch ? ManifestFileResult::HashMismatch : result == FileIntegrityResult::StorageFailure ? ManifestFileResult::StorageFailure
                                                                                                                                              : ManifestFileResult::Invalid;
    }
    UploadedManifestReader<Io, Hash, 4166> reader_;
    ManifestFileCheck<Io, Hash> file_;
    Hash& hash_;
    Crypto& crypto_;
    const UploadJournal& kml_upload_;
    const uint32_t& generation_;
    const uint8_t* actor_hash_;
    VerifiedManifestView& verified_;
    uint8_t* workspace_;
    size_t workspace_capacity_;
    geocaching::ByteView expected_object_, expected_revision_;
    const char* kml_path_ = nullptr;
    ManifestFileResult result_ = ManifestFileResult::Idle;
    bool authenticated_ = false, checking_kml_ = false;
};
} // namespace track_sharing
