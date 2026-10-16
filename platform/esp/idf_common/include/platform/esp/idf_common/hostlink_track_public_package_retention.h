#pragma once
#include "platform/esp/idf_common/hostlink_track_attachment_retention.h"
#include "platform/esp/idf_common/hostlink_track_package_input_owner.h"
namespace hostlink::management::package_install_runtime
{
// Retains original signed bytes and full supplied package in the public cache.
// This creates no owned publication head and exposes no /routes entry yet.
class TrackPublicPackageRetention
{
  public:
    TrackPublicPackageRetention(TrackPackageInputOwner& input, uint32_t media) : input_(input), media_(media), copy_(input.io(), input.hash()) {}
    ~TrackPublicPackageRetention()
    {
        if (attachments_)
        {
            attachments_->~TrackAttachmentRetention();
            heap_caps_free(attachments_);
        }
    }
    Status step(uint32_t generation, const uint8_t* actor, uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        const auto current = input_.step(generation, actor, scratch, capacity);
        if (current != ManifestFileResult::Ready || !input_.ready() || media_ != sd::sd_media_session()) return fail(current == ManifestFileResult::StorageFailure || media_ != sd::sd_media_session() ? Status::StorageUnavailable : Status::StaleIdentity);
        if (terminal_) return status_;
        const auto* verified = input_.verified();
        const geocaching::ByteView revision{verified->revision_hash, 32};
        if (attachments_)
        {
            const auto result = attachments_->step(scratch, capacity);
            if (result == Status::Busy) return result;
            return result == Status::Ok ? finish() : fail(result);
        }
        if (stage_ == 2)
        {
            if (!input_.attachments()) return finish();
            void* memory = heap_caps_malloc(sizeof(TrackAttachmentRetention), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!memory) return Status::Busy;
            attachments_ = new (memory) TrackAttachmentRetention(*input_.attachments(), revision, media_, true);
            return Status::Busy;
        }
        if (!copying_)
        {
            auto admitted = prepare_runtime::ensurePackageCacheDirectory(media_, revision, scratch, capacity);
            if (admitted != Status::Ok) return admitted == Status::Busy ? admitted : fail(admitted);
            const bool signed_stage = stage_ == 0;
            const auto final_kind = signed_stage ? ObjectFile::SignedManifest : ObjectFile::KmlFile;
            const auto temporary_kind = signed_stage ? ObjectFile::SignedTemporary : ObjectFile::KmlTemporary;
            const auto bytes = signed_stage ? input_.signedUpload().total : verified->manifest.file_bytes;
            const geocaching::ByteView hash = signed_stage ? geocaching::ByteView{input_.signedUpload().file_hash, 32} : verified->manifest.file_hash;
            admitted = prepare_runtime::admitPackageCacheFile(media_, revision, hash, bytes, final_kind, temporary_kind, scratch, capacity);
            if (admitted != Status::Ok) return admitted == Status::Busy ? admitted : fail(admitted);
            if (!resolvePackageCachePath(revision, final_kind, {}, destination_, sizeof(destination_)) || !resolvePackageCachePath(revision, temporary_kind, {}, temporary_, sizeof(temporary_))) return fail(Status::InvalidField);
            const auto begin = copy_.begin(signed_stage ? input_.signedPath() : input_.kmlPath(), temporary_, destination_, bytes, hash.data);
            if (begin != ImmutableCopyResult::Pending) return fail(map(begin));
            copying_ = true;
        }
        const auto copied = copy_.step(scratch, capacity);
        if (copied == ImmutableCopyResult::Pending) return Status::Busy;
        if (copied != ImmutableCopyResult::Ready) return fail(map(copied));
        copying_ = false;
        ++stage_;
        return Status::Busy;
    }
    bool ready() const { return terminal_ && status_ == Status::Ok && input_.ready(); }

  private:
    using TrackAttachmentRetention = prepare_runtime::TrackAttachmentRetention;
    static Status map(track_sharing::ImmutableCopyResult result) { return result == track_sharing::ImmutableCopyResult::HashMismatch ? Status::HashMismatch : result == track_sharing::ImmutableCopyResult::StorageFailure ? Status::StorageUnavailable
                                                                                                                                                                                                                           : Status::Conflict; }
    Status fail(Status result)
    {
        copy_.cancel();
        terminal_ = true;
        return status_ = result;
    }
    Status finish()
    {
        terminal_ = true;
        return status_ = Status::Ok;
    }
    TrackPackageInputOwner& input_;
    uint32_t media_;
    track_sharing::ImmutableFileCopy<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> copy_;
    char temporary_[160]{}, destination_[160]{};
    TrackAttachmentRetention* attachments_ = nullptr;
    Status status_ = Status::Busy;
    uint8_t stage_ = 0;
    bool copying_ = false, terminal_ = false;
};
static_assert(sizeof(TrackPublicPackageRetention) <= 768, "Public retention owns no payload/context/index buffers");
inline TrackPublicPackageRetention* createPublicPackageRetention(TrackPackageInputOwner& input, uint32_t media)
{
    if (!input.ready() || media != sd::sd_media_session()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackPublicPackageRetention), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? new (memory) TrackPublicPackageRetention(input, media) : nullptr;
}
inline void destroyPublicPackageRetention(TrackPublicPackageRetention* work)
{
    if (work)
    {
        work->~TrackPublicPackageRetention();
        heap_caps_free(work);
    }
}
} // namespace hostlink::management::package_install_runtime
