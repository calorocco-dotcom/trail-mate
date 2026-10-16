#pragma once
#include "track_sharing/manifest_file_check.h"
#include "track_sharing/prepare_request.h"
#include "track_sharing/uploaded_manifest_reader.h"
namespace track_sharing
{
// One admitted preparation job borrows paths, identity, journals, manifest
// output and XML name stack. No second hash context or owned manifest buffer.
template <class Io, class Hash>
class UploadedCandidateCheck
{
  public:
    UploadedCandidateCheck(Io& io, Hash& hash, const UploadJournal& manifest_upload, const UploadJournal& kml_upload,
                           const uint32_t& generation, const uint8_t* author_hash, void* names, size_t capacity, ManifestView& view)
        : reader_(io, hash, manifest_upload, generation, author_hash), file_(io, hash, names, capacity),
          manifest_upload_(manifest_upload), kml_upload_(kml_upload), generation_(generation), author_hash_(author_hash), view_(view) {}
    ManifestFileResult begin(const PrepareRequest& request, const char* manifest_path, const char* kml_path, uint8_t* output, size_t capacity, geocaching::ByteView author)
    {
        if (result_ != ManifestFileResult::Idle) return ManifestFileResult::Busy;
        if (!kml_path || !author.data || author.size != 64 || !uploadJournalValid(kml_upload_) || kml_upload_.phase != UploadPhase::Sealed ||
            !std::memcmp(manifest_upload_.id, kml_upload_.id, 16)) return ManifestFileResult::Invalid;
        if (!request.operation.data || request.operation.size != 16 || !request.kml_upload.data || request.kml_upload.size != 16 ||
            !request.manifest_upload.data || request.manifest_upload.size != 16 || request.generation != generation_ ||
            std::memcmp(request.kml_upload.data, kml_upload_.id, 16) || std::memcmp(request.manifest_upload.data, manifest_upload_.id, 16) ||
            (request.expected_parent.size && (!request.expected_parent.data || request.expected_parent.size != 32))) return ManifestFileResult::Invalid;
        uint8_t operation_bits = 0;
        for (size_t i = 0; i < 16; ++i) operation_bits |= request.operation.data[i];
        if (!operation_bits) return ManifestFileResult::Invalid;
        const auto read = reader_.begin(manifest_path, output, capacity);
        if (read != FileIntegrityResult::Pending) return result_ = map(read);
        kml_path_ = kml_path;
        author_ = author;
        expected_parent_ = request.expected_parent;
        return result_ = ManifestFileResult::Pending;
    }
    ManifestFileResult step(uint8_t* scratch, size_t capacity)
    {
        KmlOnlyPackage denied;
        return step(scratch, capacity, denied);
    }
    template <class Package>
    ManifestFileResult step(uint8_t* scratch, size_t capacity, Package& package)
    {
        if (result_ != ManifestFileResult::Pending) return result_;
        if (!author_hash_ || generation_ != kml_upload_.generation || std::memcmp(author_hash_, kml_upload_.author_hash, 32))
        {
            reader_.cancel();
            file_.cancel();
            return result_ = ManifestFileResult::Invalid;
        }
        if (!checking_file_)
        {
            const auto read = reader_.step(scratch, capacity);
            if (read == FileIntegrityResult::Pending) return result_;
            if (read != FileIntegrityResult::Ready) return result_ = map(read);
            if (!decodeManifest(reader_.bytes(), view_) || std::memcmp(view_.author.data, author_.data, 64) ||
                view_.file_bytes != kml_upload_.total || std::memcmp(view_.file_hash.data, kml_upload_.file_hash, 32)) return result_ = ManifestFileResult::Invalid;
            if (view_.version == 2)
            {
                const auto checked = package.checkAttachments(view_, scratch, capacity);
                if (checked == ManifestFileResult::Pending) return result_;
                if (checked != ManifestFileResult::Ready) return result_ = checked;
                result_ = file_.beginUploadedPackageCandidate(kml_path_, reader_.bytes(), author_, kml_upload_, generation_, {author_hash_, 32}, view_, package);
            }
            else result_ = file_.beginUploadedCandidate(kml_path_, reader_.bytes(), author_, kml_upload_, generation_, {author_hash_, 32}, view_);
            if (result_ == ManifestFileResult::Pending &&
                (view_.revision == 1 ? expected_parent_.size != 0 : expected_parent_.size != 32 || std::memcmp(expected_parent_.data, view_.previous.data, 32)))
            {
                file_.cancel();
                return result_ = ManifestFileResult::Invalid;
            }
            checking_file_ = result_ == ManifestFileResult::Pending;
            return result_;
        }
        return result_ = file_.step(scratch, capacity);
    }
    ManifestFileResult cancel()
    {
        if (result_ == ManifestFileResult::Pending)
        {
            reader_.cancel();
            file_.cancel();
            result_ = ManifestFileResult::Cancelled;
        }
        return result_;
    }
    bool ready() const { return result_ == ManifestFileResult::Ready && file_.ready(); }
    bool matchesCandidate(const ManifestView& candidate) const { return ready() && file_.matchesCandidate(candidate); }
    geocaching::ByteView manifest() const { return ready() ? reader_.bytes() : geocaching::ByteView{}; }

  private:
    struct KmlOnlyPackage
    {
        ManifestFileResult checkAttachments(const ManifestView&, uint8_t*, size_t) { return ManifestFileResult::Invalid; }
        bool matchesAttachments(const ManifestView&) const { return false; }
    };
    static ManifestFileResult map(FileIntegrityResult result)
    {
        return result == FileIntegrityResult::HashMismatch ? ManifestFileResult::HashMismatch : result == FileIntegrityResult::StorageFailure ? ManifestFileResult::StorageFailure
                                                                                            : result == FileIntegrityResult::Cancelled        ? ManifestFileResult::Cancelled
                                                                                                                                              : ManifestFileResult::Invalid;
    }
    UploadedManifestReader<Io, Hash> reader_;
    ManifestFileCheck<Io, Hash> file_;
    const UploadJournal& manifest_upload_;
    const UploadJournal& kml_upload_;
    const uint32_t& generation_;
    const uint8_t* author_hash_;
    ManifestView& view_;
    const char* kml_path_ = nullptr;
    geocaching::ByteView author_, expected_parent_;
    ManifestFileResult result_ = ManifestFileResult::Idle;
    bool checking_file_ = false;
};
} // namespace track_sharing
