#pragma once
#include "platform/esp/idf_common/hostlink_track_prepare_owner.h"
#include "track_sharing/immutable_file_copy.h"
#include "track_sharing/object_paths.h"
namespace hostlink::management::prepare_runtime
{
// Embedded in an admitted transient stage; borrows input's exclusive Io/hash.
// Caller reserves directory space and retains input/media/identity until done.
class TrackKmlRetention
{
  public:
    explicit TrackKmlRetention(TrackPrepareOwner& input) : input_(input), copy_(input.io, input.hash) {}
    track_sharing::ImmutableCopyResult begin(geocaching::ByteView object)
    {
        using namespace track_sharing;
        if (!input_.ready() || !resolveObjectPath(object, ObjectFile::KmlTemporary, input_.view.file_hash, temporary_, sizeof(temporary_)) ||
            !resolveObjectPath(object, ObjectFile::KmlFile, input_.view.file_hash, destination_, sizeof(destination_))) return ImmutableCopyResult::Invalid;
        return copy_.begin(input_.kml_path, temporary_, destination_, input_.view.file_bytes, input_.view.file_hash.data);
    }
    track_sharing::ImmutableCopyResult step(uint8_t* scratch, size_t capacity)
    {
        if (!input_.ready()) { copy_.cancel(); return track_sharing::ImmutableCopyResult::StorageFailure; }
        return copy_.step(scratch, capacity);
    }
    const char* destination() const { return destination_; }
  private:
    TrackPrepareOwner& input_;
    char temporary_[160]{}, destination_[160]{};
    track_sharing::ImmutableFileCopy<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> copy_;
};
static_assert(sizeof(TrackKmlRetention) <= 560, "Retention must not duplicate KML or hash context");
} // namespace hostlink::management::prepare_runtime
