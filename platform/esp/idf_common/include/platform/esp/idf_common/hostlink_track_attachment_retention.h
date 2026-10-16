#pragma once
#include "platform/esp/idf_common/hostlink_track_attachment_prepare_owner.h"
#include "platform/esp/idf_common/hostlink_track_package_cache_admission.h"
#include "platform/esp/idf_common/hostlink_track_package_retention_admission.h"
#include "track_sharing/immutable_file_copy.h"
namespace hostlink::management::prepare_runtime
{
// Copies verified index first, then streams that retained index and copies
// supplied resources one at a time. All target paths derive from object/hash.
// Existing/partial destinations are rechecked by ImmutableFileCopy on retry.
class TrackAttachmentRetention
{
  public:
    TrackAttachmentRetention(TrackAttachmentPrepareOwner& source, geocaching::ByteView object, uint32_t media, bool public_cache = false)
        : source_(source), media_(media), index_(source.io(), source.indexHash(), window_, sizeof(window_), fingerprints_, 64,
                                                 source.indexBinding(), source.kmlHash(), source.kmlBytes()),
          copy_(source.io(), source.resourceHash()), public_cache_(public_cache)
    {
        if (object.data && object.size == 32) std::memcpy(object_, object.data, 32);
    }
    Status step(uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (!source_.ready() || !source_.mediaReady() || media_ != sd::sd_media_session()) return stop(Status::StorageUnavailable);
        if (terminal_) return status_;
        if (!index_retained_)
        {
            if (!copying_)
            {
                const auto binding = source_.indexBinding();
                const auto admitted = beginCopy(source_.indexPath(), binding.hash, binding.bytes, ObjectFile::AttachmentIndex, ObjectFile::AttachmentIndexTemporary, scratch, capacity);
                if (admitted != Status::Ok) return admitted == Status::Busy ? admitted : stop(admitted);
            }
            const auto copied = copy_.step(scratch, capacity);
            if (copied == ImmutableCopyResult::Pending) return Status::Busy;
            if (copied != ImmutableCopyResult::Ready) return stop(map(copied));
            copying_ = false;
            index_retained_ = true;
            std::memcpy(index_path_, destination_, sizeof(index_path_));
            if (index_.begin(index_path_) != AttachmentIndexFileResult::Pending) return stop(Status::InvalidField);
            return Status::Busy;
        }
        if (copying_)
        {
            const auto copied = copy_.step(scratch, capacity);
            if (copied == ImmutableCopyResult::Pending) return Status::Busy;
            if (copied != ImmutableCopyResult::Ready) return stop(map(copied));
            copying_ = false;
            return next();
        }
        const auto parsed = index_.step();
        if (parsed == AttachmentIndexFileResult::Pending) return Status::Busy;
        if (parsed == AttachmentIndexFileResult::Ready)
        {
            terminal_ = true;
            return status_ = Status::Ok;
        }
        if (parsed != AttachmentIndexFileResult::Entry) return stop(parsed == AttachmentIndexFileResult::HashMismatch ? Status::HashMismatch : Status::StorageUnavailable);
        const auto* entry = index_.entry();
        if (!entry || ordinal_ >= 64) return stop(Status::InvalidField);
        const auto resolved = source_.resolver().resolve(ordinal_, *entry, upload_path_, sizeof(upload_path_));
        if (resolved == AttachmentResolveResult::Missing)
            return entry->required ? stop(Status::Conflict) : next();
        if (resolved != AttachmentResolveResult::Ready) return stop(resolved == AttachmentResolveResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict);
        const auto admitted = beginCopy(upload_path_, entry->hash, entry->bytes, ObjectFile::AttachmentResource, ObjectFile::AttachmentResourceTemporary, scratch, capacity);
        return admitted == Status::Ok || admitted == Status::Busy ? Status::Busy : stop(admitted);
    }

  private:
    Status beginCopy(const char* source, geocaching::ByteView hash, uint32_t bytes, track_sharing::ObjectFile final_kind,
                     track_sharing::ObjectFile temporary_kind, uint8_t* scratch, size_t capacity)
    {
        const auto admitted = public_cache_ ? admitPackageCacheFile(media_, {object_, 32}, hash, bytes, final_kind, temporary_kind, scratch, capacity) : admitRetainedPackageFile(media_, {object_, 32}, hash, bytes, final_kind, temporary_kind, scratch, capacity);
        if (admitted != Status::Ok) return admitted;
        const auto resolve = [&](track_sharing::ObjectFile kind, char* path, size_t capacity)
        {
            if (public_cache_)
            {
                const bool resource = kind == track_sharing::ObjectFile::AttachmentResource || kind == track_sharing::ObjectFile::AttachmentResourceTemporary;
                return track_sharing::resolvePackageCachePath({object_, 32}, kind, resource ? hash : geocaching::ByteView{}, path, capacity);
            }
            return track_sharing::resolveObjectPath({object_, 32}, kind, hash, path, capacity);
        };
        if (!resolve(final_kind, destination_, sizeof(destination_)) || !resolve(temporary_kind, temporary_, sizeof(temporary_))) return Status::InvalidField;
        const auto begun = copy_.begin(source, temporary_, destination_, bytes, hash.data);
        copying_ = begun == track_sharing::ImmutableCopyResult::Pending;
        return copying_ ? Status::Ok : map(begun);
    }
    Status next()
    {
        if (!index_.advance()) return stop(Status::InvalidField);
        ++ordinal_;
        return Status::Busy;
    }
    static Status map(track_sharing::ImmutableCopyResult result)
    {
        return result == track_sharing::ImmutableCopyResult::HashMismatch ? Status::HashMismatch : result == track_sharing::ImmutableCopyResult::StorageFailure ? Status::StorageUnavailable
                                                                                                                                                                : Status::Conflict;
    }
    Status stop(Status result)
    {
        index_.cancel();
        copy_.cancel();
        terminal_ = true;
        return status_ = result;
    }
    TrackAttachmentPrepareOwner& source_;
    uint32_t media_;
    uint8_t object_[32]{}, window_[4352]{};
    track_sharing::AttachmentFingerprint fingerprints_[64]{};
    char temporary_[160]{}, destination_[160]{}, index_path_[160]{}, upload_path_[64]{};
    track_sharing::AttachmentIndexFileCheck<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> index_;
    track_sharing::ImmutableFileCopy<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> copy_;
    uint8_t ordinal_ = 0;
    Status status_ = Status::Busy;
    bool index_retained_ = false, copying_ = false, terminal_ = false;
    bool public_cache_;
};
static_assert(sizeof(TrackAttachmentRetention) <= 7168, "Attachment retention must fit transient PSRAM admission");
} // namespace hostlink::management::prepare_runtime
