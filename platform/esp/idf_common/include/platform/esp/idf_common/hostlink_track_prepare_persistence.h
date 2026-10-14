#pragma once
#include "chat/ports/i_mesh_adapter.h"
#include "platform/esp/common/storage/track_manifest_crypto.h"
#include "platform/esp/idf_common/hostlink_track_object_admission.h"
#include "platform/esp/idf_common/hostlink_track_prepare_owner.h"
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
#include "track_sharing/checked_reservation.h"
#include "track_sharing/current_parent_loader.h"
#include "track_sharing/persist_signed_head.h"
#include "track_sharing/prepared_signature.h"
#include "track_sharing/restore_prepared_signature.h"
#include "track_sharing/signed_manifest_reader.h"
namespace hostlink::management::prepare_runtime
{
// Scheduler lends a Ready input owner and holds exclusive object/media/identity
// leases throughout this stage. No public capability is enabled by this class.
class TrackPreparePersistence
{
  public:
    explicit TrackPreparePersistence(TrackPrepareOwner& input)
        : input_(input), heads_(input.io, head_a_, head_b_), reservations_(input.io, reservation_a_, reservation_b_),
          blobs_(input.io, signed_temporary_, signed_path_),
          parent_loader_(input.io, input.hash, crypto_, heads_, head_, head_verify_, verified_, parent_path_, signed_bytes_, sizeof(signed_bytes_)) {}
    bool initialize()
    {
        if (!input_.ready() || !track_sharing::deriveManifestHashes(input_.manifest(), input_.view, input_.hash, object_, candidate_)) return false;
        using track_sharing::ObjectFile;
        const geocaching::ByteView object{object_, 32}, operation = input_.request.operation, revision{candidate_, 32};
        initialized_ = track_sharing::resolveObjectPath(object, ObjectFile::HeadA, {}, head_a_, sizeof(head_a_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::HeadB, {}, head_b_, sizeof(head_b_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::ReservationA, operation, reservation_a_, sizeof(reservation_a_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::ReservationB, operation, reservation_b_, sizeof(reservation_b_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::SignedTemporary, revision, signed_temporary_, sizeof(signed_temporary_)) &&
                       track_sharing::resolveObjectPath(object, ObjectFile::SignedManifest, revision, signed_path_, sizeof(signed_path_));
        return initialized_;
    }
    Status finish(chat::IMeshAdapter& adapter, const track_sharing::VerifiedManifestView* parent, uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        complete_ = false;
        if (!initialized_ || !input_.ready() || !scratch || capacity < 448) return Status::InvalidField;
        uint8_t current_hash[32];
        uint32_t current_generation = 0;
        auto* key = scratch + capacity - 64;
        const auto identity = upload_runtime::readUploadAuthor(adapter, key, 64, current_hash, current_generation);
        if (identity != Status::Ok) return identity;
        if (current_generation != input_.generation || std::memcmp(key, input_.author, 64) || std::memcmp(current_hash, input_.author_hash, 32)) return Status::StaleIdentity;
        if (parent_loading_)
        {
            const auto loaded_parent = parent_loader_.step(workspace_, sizeof(workspace_));
            if (loaded_parent == ParentLoadResult::Pending) return Status::Busy;
            parent_loading_ = false;
            if (loaded_parent != ParentLoadResult::Ready) return loaded_parent == ParentLoadResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
            parent_ready_ = true;
        }
        const auto loaded_head = heads_.load(head_, head_verify_, scratch, capacity);
        if (loaded_head != HeadStoreResult::Ready && loaded_head != HeadStoreResult::Missing) return loaded_head == HeadStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        const auto loaded_reservation = reservations_.load(reservation_, reservation_verify_, scratch, capacity);
        if (loaded_reservation != ReservationStoreResult::Ready && loaded_reservation != ReservationStoreResult::Missing) return loaded_reservation == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        const geocaching::ByteView author{input_.author, 64}, author_hash{input_.author_hash, 32};
        if (loaded_reservation == ReservationStoreResult::Ready)
        {
            if (!reservationMatchesInput()) return Status::Conflict;
            if (reservation_.phase == ReservationPhase::Checked || reservation_.phase == ReservationPhase::Signed)
            {
                if (loaded_head == HeadStoreResult::Missing)
                {
                    const auto admitted = requireOwnedFirstRevisionDirectory(input_.media, input_.request, input_.author_hash, reservation_, reservations_, reservation_verify_, scratch, capacity);
                    if (admitted != Status::Ok) return admitted;
                }
                const auto existing = readVerifiedSignature();
                if (existing == Status::Ok)
                {
                    const auto restored = restorePreparedSignature(input_, verified_, author, input_.generation, author_hash, reservation_, reservation_verify_, reservations_, workspace_, sizeof(workspace_), signature_, sizeof(signature_));
                    if (restored != PreparedSignatureResult::Ready) return restored == PreparedSignatureResult::StorageFailure ? Status::StorageUnavailable : restored == PreparedSignatureResult::StaleIdentity ? Status::StaleIdentity
                                                                                                                                                                                                                 : Status::Conflict;
                    return persistHead(scratch, capacity);
                }
                if (existing != Status::Expired) return existing;
                if (reservation_.phase == ReservationPhase::Signed) return Status::StorageUnavailable;
            }
        }
        if (loaded_head == HeadStoreResult::Missing)
        {
            if (parent || input_.request.expected_parent.size) return Status::Conflict;
            Status admitted;
            if (loaded_reservation == ReservationStoreResult::Missing)
                admitted = requireEmptyObjectDirectory(input_.media, {object_, 32}, scratch, capacity);
            else if (loaded_reservation == ReservationStoreResult::Ready && !std::memcmp(reservation_.object, object_, 32) && !std::memcmp(reservation_.candidate, candidate_, 32))
                admitted = requireOwnedFirstRevisionDirectory(input_.media, input_.request, input_.author_hash, reservation_, reservations_, reservation_verify_, scratch, capacity);
            else return loaded_reservation == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
            if (admitted != Status::Ok) return admitted;
        }
        else if (!parent)
        {
            if (parent_ready_) parent = &verified_;
            else
            {
                const auto started = parent_loader_.begin({object_, 32}, input_.request.expected_parent, {input_.author, 64}, input_.generation, scratch, capacity);
                if (started != ParentLoadResult::Pending) return started == ParentLoadResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
                parent_loading_ = true;
                return Status::Busy;
            }
        }
        const auto directory = ensureObjectDirectory(input_.media, {object_, 32}, scratch, capacity);
        if (directory != Status::Ok) return directory;
        const auto checked = persistCheckedReservation(input_, input_.request, input_.manifest(), author, author_hash, input_.generation,
                                                       loaded_head == HeadStoreResult::Missing ? nullptr : &head_, parent, input_.hash, reservations_, input_.view,
                                                       desired_, reservation_, reservation_verify_, scratch, capacity);
        if (checked != ReservationStoreResult::Ready) return checked == ReservationStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        const auto signed_result = persistPreparedSignature(input_, input_.manifest(), author, input_.generation, author_hash, reservation_, reservation_verify_,
                                                            input_.hash, adapter, blobs_, reservations_, input_.view, workspace_, sizeof(workspace_), signature_, sizeof(signature_));
        if (signed_result != PreparedSignatureResult::Ready) return signed_result == PreparedSignatureResult::StaleIdentity ? Status::StaleIdentity : signed_result == PreparedSignatureResult::StorageFailure ? Status::StorageUnavailable
                                                                                                                                                  : signed_result == PreparedSignatureResult::Unavailable      ? Status::Busy
                                                                                                                                                                                                               : Status::Conflict;
        const auto verified = readVerifiedSignature();
        if (verified != Status::Ok) return verified == Status::Expired ? Status::StorageUnavailable : verified;
        return persistHead(scratch, capacity);
    }
    bool ready() const { return complete_ && input_.ready(); }
    const track_sharing::ObjectHead& head() const { return head_; }
    geocaching::ByteView signature() const { return complete_ ? geocaching::ByteView{signature_, sizeof(signature_)} : geocaching::ByteView{}; }

  private:
    bool reservationMatchesInput() const
    {
        if (reservation_.generation != input_.generation || reservation_.revision != input_.view.revision ||
            std::memcmp(reservation_.operation, input_.request.operation.data, 16) || std::memcmp(reservation_.object, object_, 32) ||
            std::memcmp(reservation_.candidate, candidate_, 32) || std::memcmp(reservation_.author, input_.author_hash, 32)) return false;
        return reservation_.revision == 1 ? input_.view.previous.size == 0 : input_.view.previous.size == 32 && !std::memcmp(reservation_.parent, input_.view.previous.data, 32);
    }
    Status readVerifiedSignature()
    {
        using namespace track_sharing;
        const auto probe = input_.io.open(signed_path_, sd::SdBorrowedRecordFileIo::Mode::Read);
        if (!probe.handle) return probe.status == sd::SdBorrowedRecordFileIo::OpenStatus::Missing ? Status::Expired : Status::StorageUnavailable;
        if (!input_.io.close(probe.handle)) return Status::StorageUnavailable;
        SignedManifestReader<sd::SdBorrowedRecordFileIo> reader(input_.io);
        if (reader.begin(signed_path_, signed_bytes_, sizeof(signed_bytes_)) != FileIntegrityResult::Pending) return Status::StorageUnavailable;
        FileIntegrityResult read;
        do
        {
            read = reader.step();
        } while (read == FileIntegrityResult::Pending);
        if (read != FileIntegrityResult::Ready) return Status::StorageUnavailable;
        return verifyManifest(reader.bytes(), crypto_, input_.hash, workspace_, sizeof(workspace_), verified_, {input_.author, 64}) == ManifestVerification::Valid ? Status::Ok : Status::Conflict;
    }
    Status persistHead(uint8_t* scratch, size_t capacity)
    {
        const auto persisted = track_sharing::persistSignedObjectHead(heads_, verified_, reservation_, input_.generation, {input_.author, 64}, {input_.author_hash, 32},
                                                                      head_, head_next_, head_verify_, scratch, capacity);
        if (persisted != track_sharing::HeadStoreResult::Ready) return persisted == track_sharing::HeadStoreResult::StorageFailure ? Status::StorageUnavailable : Status::Conflict;
        complete_ = true;
        return Status::Ok;
    }
    TrackPrepareOwner& input_;
    uint8_t object_[32]{}, candidate_[32]{}, signature_[64]{}, workspace_[4200]{}, signed_bytes_[4166]{};
    char head_a_[160]{}, head_b_[160]{}, reservation_a_[160]{}, reservation_b_[160]{}, signed_temporary_[160]{}, signed_path_[160]{}, parent_path_[160]{};
    track_sharing::ObjectHead head_, head_next_, head_verify_;
    track_sharing::RevisionReservation desired_, reservation_, reservation_verify_;
    track_sharing::VerifiedManifestView verified_;
    platform::esp::storage::TrackManifestCrypto crypto_;
    track_sharing::ObjectHeadStore<sd::SdBorrowedRecordFileIo> heads_;
    track_sharing::ReservationStore<sd::SdBorrowedRecordFileIo> reservations_;
    track_sharing::SignedManifestStore<sd::SdBorrowedRecordFileIo> blobs_;
    track_sharing::CurrentParentLoader<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256,
                                       platform::esp::storage::TrackManifestCrypto, track_sharing::ObjectHeadStore<sd::SdBorrowedRecordFileIo>>
        parent_loader_;
    bool initialized_ = false, complete_ = false, parent_loading_ = false, parent_ready_ = false;
};
static_assert(sizeof(TrackPreparePersistence) <= 12288, "Persistent preparation must fit transient PSRAM admission");
inline TrackPreparePersistence* createPreparePersistence(TrackPrepareOwner& input)
{
    if (!input.ready()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackPreparePersistence), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* task = new (memory) TrackPreparePersistence(input);
    if (task->initialize()) return task;
    task->~TrackPreparePersistence();
    heap_caps_free(task);
    return nullptr;
}
inline void destroyPreparePersistence(TrackPreparePersistence* task)
{
    if (!task) return;
    task->~TrackPreparePersistence();
    heap_caps_free(task);
}
} // namespace hostlink::management::prepare_runtime
