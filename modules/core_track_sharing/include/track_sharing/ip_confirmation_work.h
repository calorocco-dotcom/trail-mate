#pragma once
#include "track_sharing/confirm_ip_publication_head.h"
#include "track_sharing/object_paths.h"
#include "track_sharing/reservation_store.h"
#include "track_sharing/restore_ip_publication_head.h"
#include "track_sharing/signed_manifest_reader.h"
#include "track_sharing/upload_journal_store.h"
#include "track_sharing/upload_paths.h"
#include "track_sharing/uploaded_manifest_reader.h"
#include "track_sharing/verify_ip_confirmation.h"
namespace track_sharing
{
enum class IpConfirmationResult : uint8_t
{
    Pending,
    Ready,
    Invalid,
    Conflict,
    HashMismatch,
    StorageFailure,
    StaleIdentity,
    Cancelled
};
// Platform admits this metadata owner and separate payload/domain storage in
// transient PSRAM, never on an ESP task stack. Each step gets fresh identity,
// media admission and disjoint scratch. Caller holds exclusive object paths;
// no file handles survive steps, no RF publication is admitted here.
template <class Io, class Hash, class ManifestCrypto, class ReceiptCrypto>
class IpConfirmationWork
{
  public:
    IpConfirmationWork(Io& io, Hash& hash, ManifestCrypto& manifest_crypto, ReceiptCrypto& receipt_crypto,
                       const IpConfirmationRequest& request, geocaching::ByteView author, geocaching::ByteView author_hash,
                       uint8_t* signed_storage, size_t signed_capacity, uint8_t* receipt_storage, size_t receipt_capacity,
                       uint8_t* domain_storage, size_t domain_capacity)
        : io_(io), hash_(hash), manifest_crypto_(manifest_crypto), receipt_crypto_(receipt_crypto),
          heads_(io, head_a_, head_b_), reservations_(io, reservation_a_, reservation_b_),
          uploads_(io, upload_a_, upload_b_), receipts_(io, receipt_temporary_, receipt_path_),
          signed_reader_(io), receipt_reader_(io, hash, upload_, generation_, author_hash_),
          import_reader_(io, hash, upload_, generation_, author_hash_),
          signed_storage_(signed_storage), receipt_storage_(receipt_storage), domain_storage_(domain_storage),
          domain_capacity_(domain_capacity)
    {
        if (!author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 ||
            !signed_storage || signed_capacity < 4166 || !receipt_storage || receipt_capacity < 1100 ||
            !domain_storage || domain_capacity < 4166 || !request.generation || !request.revision) return;
        const geocaching::ByteView buffers[] = {{signed_storage, 4166}, {receipt_storage, 1100}, {domain_storage, domain_capacity}};
        for (const auto buffer : buffers)
            if (signingSpansOverlap(buffer.data, buffer.size, reinterpret_cast<const uint8_t*>(this), sizeof(*this))) return;
        if (signingSpansOverlap(signed_storage, 4166, receipt_storage, 1100) ||
            signingSpansOverlap(signed_storage, 4166, domain_storage, domain_capacity) ||
            signingSpansOverlap(receipt_storage, 1100, domain_storage, domain_capacity)) return;
        size_t offset = 0;
        const auto copy = [&](geocaching::ByteView input, size_t length, geocaching::ByteView& output)
        {
            if (!input.data || input.size != length) return false;
            std::memcpy(bindings_ + offset, input.data, length);
            output = {bindings_ + offset, length};
            offset += length;
            return true;
        };
        if (!copy(request.operation, 16, request_.operation) || !copy(request.transfer, 16, request_.transfer) ||
            !copy(request.proof_hash, 32, request_.proof_hash) || !copy(request.directory_public, 64, request_.directory_public) ||
            !copy(request.directory, 16, request_.directory) || !copy(request.epoch, 16, request_.epoch) ||
            !copy(request.network_request, 16, request_.network_request) || !copy(request.object, 32, request_.object) ||
            !copy(request.revision_hash, 32, request_.revision_hash)) return;
        if (request.manifest_transfer.size && (!copy(request.manifest_transfer, 16, request_.manifest_transfer) ||
                                               !copy(request.manifest_hash, 32, request_.manifest_hash) || !copy(request.manifest_signature, 64, request_.manifest_signature))) return;
        std::memcpy(author_, author.data, 64);
        std::memcpy(author_hash_, author_hash.data, 32);
        generation_ = request_.generation = request.generation;
        request_.revision = request.revision;
        valid_ = resolveObjectPath(request_.object, ObjectFile::HeadA, {}, head_a_, sizeof(head_a_)) &&
                 resolveObjectPath(request_.object, ObjectFile::HeadB, {}, head_b_, sizeof(head_b_)) &&
                 resolveObjectPath(request_.object, ObjectFile::ReservationA, request_.operation, reservation_a_, sizeof(reservation_a_)) &&
                 resolveObjectPath(request_.object, ObjectFile::ReservationB, request_.operation, reservation_b_, sizeof(reservation_b_)) &&
                 resolveObjectPath(request_.object, ObjectFile::SignedManifest, request_.revision_hash, signed_path_, sizeof(signed_path_)) &&
                 resolveObjectPath(request_.object, ObjectFile::ReceiptTemporary, request_.network_request, receipt_temporary_, sizeof(receipt_temporary_)) &&
                 resolveObjectPath(request_.object, ObjectFile::Receipt, request_.network_request, receipt_path_, sizeof(receipt_path_)) &&
                 resolveUploadPath(request_.transfer.data, 16, UploadFile::Candidate, upload_path_, sizeof(upload_path_), UploadKind::IpReceipt) &&
                 resolveUploadPath(request_.transfer.data, 16, UploadFile::JournalA, upload_a_, sizeof(upload_a_), UploadKind::IpReceipt) &&
                 resolveUploadPath(request_.transfer.data, 16, UploadFile::JournalB, upload_b_, sizeof(upload_b_), UploadKind::IpReceipt);
        if (valid_ && request_.manifest_transfer.size)
            valid_ = resolveObjectPath(request_.object, ObjectFile::SignedTemporary, request_.revision_hash, reservation_a_, sizeof(reservation_a_));
        if (valid_) result_ = IpConfirmationResult::Pending;
    }
    ~IpConfirmationWork()
    {
        if (valid_) wipe();
    }
    IpConfirmationWork(const IpConfirmationWork&) = delete;
    IpConfirmationWork& operator=(const IpConfirmationWork&) = delete;
    const ObjectHead* result() const { return result_ == IpConfirmationResult::Ready ? &head_ : nullptr; }
    const IpConfirmationRequest* importEvidence() const { return importPersistenceObject().size ? &request_ : nullptr; }
    geocaching::ByteView importPersistenceObject() const
    {
        return request_.manifest_transfer.size && stage_ == Stage::Persisting && result_ == IpConfirmationResult::Pending ? request_.object : geocaching::ByteView{};
    }
    IpConfirmationResult cancel() { return valid_ ? stop(IpConfirmationResult::Cancelled) : IpConfirmationResult::Invalid; }
    IpConfirmationResult step(uint32_t generation, geocaching::ByteView author, geocaching::ByteView author_hash,
                              bool media_ready, uint8_t* scratch, size_t capacity)
    {
        if (!valid_) return IpConfirmationResult::Invalid;
        if (!media_ready) return stop(IpConfirmationResult::StorageFailure);
        if (generation != generation_ || !author.data || author.size != 64 || !author_hash.data || author_hash.size != 32 ||
            std::memcmp(author.data, author_, 64) || std::memcmp(author_hash.data, author_hash_, 32)) return stop(IpConfirmationResult::StaleIdentity);
        if (!scratch || capacity < 320 || signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            signingSpansOverlap(scratch, capacity, signed_storage_, 4166) || signingSpansOverlap(scratch, capacity, receipt_storage_, 1100) ||
            signingSpansOverlap(scratch, capacity, domain_storage_, domain_capacity_)) return stop(IpConfirmationResult::Invalid);
        if (result_ == IpConfirmationResult::Ready)
        {
            // Cached success is bound to this exact durable head. A later
            // successor or damaged journal must never replay the old success.
            const auto current = heads_.validateCurrent(head_, head_verify_, scratch, capacity);
            if (current != HeadStoreResult::Ready) return stop(current == HeadStoreResult::StorageFailure ? IpConfirmationResult::StorageFailure : IpConfirmationResult::Conflict);
            return result_;
        }
        if (result_ != IpConfirmationResult::Pending) return result_;
        if (stage_ == Stage::Start)
        {
            if (request_.manifest_transfer.size)
            {
                if (!resolveUploadPath(request_.manifest_transfer.data, 16, UploadFile::Candidate, upload_path_, sizeof(upload_path_), UploadKind::Manifest) ||
                    !resolveUploadPath(request_.manifest_transfer.data, 16, UploadFile::JournalA, upload_a_, sizeof(upload_a_), UploadKind::Manifest) ||
                    !resolveUploadPath(request_.manifest_transfer.data, 16, UploadFile::JournalB, upload_b_, sizeof(upload_b_), UploadKind::Manifest)) return stop(IpConfirmationResult::Invalid);
                const auto loaded = uploads_.load(upload_, upload_verify_, scratch, capacity);
                if (loaded != JournalStoreResult::Ready) return stop(loaded == JournalStoreResult::StorageFailure ? IpConfirmationResult::StorageFailure : IpConfirmationResult::Conflict);
                if (upload_.phase != UploadPhase::Sealed || upload_.total > 4096 || upload_.generation != generation_ ||
                    std::memcmp(upload_.id, request_.manifest_transfer.data, 16) || std::memcmp(upload_.author_hash, author_hash_, 32) ||
                    std::memcmp(upload_.file_hash, request_.manifest_hash.data, 32)) return stop(IpConfirmationResult::Conflict);
                if (import_reader_.begin(upload_path_, signed_storage_ + 4, 4096) != FileIntegrityResult::Pending) return stop(IpConfirmationResult::Conflict);
                stage_ = Stage::ImportReading;
                return result_;
            }
            const auto loaded = uploads_.load(upload_, upload_verify_, scratch, capacity);
            if (loaded != JournalStoreResult::Ready) return stop(loaded == JournalStoreResult::StorageFailure ? IpConfirmationResult::StorageFailure : IpConfirmationResult::Conflict);
            if (upload_.phase != UploadPhase::Sealed || upload_.total > 1100 || upload_.generation != generation_ ||
                std::memcmp(upload_.id, request_.transfer.data, 16) || std::memcmp(upload_.author_hash, author_hash_, 32) ||
                std::memcmp(upload_.file_hash, request_.proof_hash.data, 32)) return stop(IpConfirmationResult::Conflict);
            const auto started = signed_reader_.begin(signed_path_, signed_storage_, 4166);
            if (started != FileIntegrityResult::Pending) return stop(IpConfirmationResult::StorageFailure);
            stage_ = Stage::SignedReading;
            return result_;
        }
        if (stage_ == Stage::ImportReading)
        {
            const auto read = import_reader_.step(scratch, capacity);
            if (read == FileIntegrityResult::Pending) return result_;
            if (read != FileIntegrityResult::Ready) return stop(read == FileIntegrityResult::HashMismatch ? IpConfirmationResult::HashMismatch : IpConfirmationResult::StorageFailure);
            const size_t length = upload_.total, prefix = length <= 255 ? 3 : 4;
            if (prefix == 3) std::memmove(signed_storage_ + 3, signed_storage_ + 4, length);
            signed_storage_[0] = 0x92;
            signed_storage_[1] = prefix == 3 ? 0xc4 : 0xc5;
            if (prefix == 3) signed_storage_[2] = uint8_t(length);
            else
            {
                signed_storage_[2] = uint8_t(length >> 8);
                signed_storage_[3] = uint8_t(length);
            }
            signed_storage_[prefix + length] = 0xc4;
            signed_storage_[prefix + length + 1] = 64;
            std::memcpy(signed_storage_ + prefix + length + 2, request_.manifest_signature.data, 64);
            if (verifyManifest({signed_storage_, prefix + length + 66}, manifest_crypto_, hash_, domain_storage_, domain_capacity_, manifest_, {author_, 64}) != ManifestVerification::Valid)
                return stop(IpConfirmationResult::Conflict);
            if (!resolveUploadPath(request_.transfer.data, 16, UploadFile::Candidate, upload_path_, sizeof(upload_path_), UploadKind::IpReceipt) ||
                !resolveUploadPath(request_.transfer.data, 16, UploadFile::JournalA, upload_a_, sizeof(upload_a_), UploadKind::IpReceipt) ||
                !resolveUploadPath(request_.transfer.data, 16, UploadFile::JournalB, upload_b_, sizeof(upload_b_), UploadKind::IpReceipt)) return stop(IpConfirmationResult::Invalid);
            const auto loaded = uploads_.load(upload_, upload_verify_, scratch, capacity);
            if (loaded != JournalStoreResult::Ready) return stop(loaded == JournalStoreResult::StorageFailure ? IpConfirmationResult::StorageFailure : IpConfirmationResult::Conflict);
            if (upload_.phase != UploadPhase::Sealed || upload_.total > 1100 || upload_.generation != generation_ ||
                std::memcmp(upload_.id, request_.transfer.data, 16) || std::memcmp(upload_.author_hash, author_hash_, 32) ||
                std::memcmp(upload_.file_hash, request_.proof_hash.data, 32)) return stop(IpConfirmationResult::Conflict);
            if (receipt_reader_.begin(upload_path_, receipt_storage_, 1100) != FileIntegrityResult::Pending) return stop(IpConfirmationResult::Conflict);
            stage_ = Stage::ReceiptReading;
            return result_;
        }
        if (stage_ == Stage::SignedReading)
        {
            const auto read = signed_reader_.step();
            if (read == FileIntegrityResult::Pending) return result_;
            if (read != FileIntegrityResult::Ready) return stop(IpConfirmationResult::StorageFailure);
            if (verifyManifest(signed_reader_.bytes(), manifest_crypto_, hash_, domain_storage_, domain_capacity_, manifest_, {author_, 64}) != ManifestVerification::Valid)
                return stop(IpConfirmationResult::Conflict);
            const auto started = receipt_reader_.begin(upload_path_, receipt_storage_, 1100);
            if (started != FileIntegrityResult::Pending) return stop(IpConfirmationResult::Conflict);
            stage_ = Stage::ReceiptReading;
            return result_;
        }
        if (stage_ == Stage::ReceiptReading)
        {
            const auto read = receipt_reader_.step(scratch, capacity);
            if (read == FileIntegrityResult::Pending) return result_;
            if (read != FileIntegrityResult::Ready) return stop(read == FileIntegrityResult::HashMismatch ? IpConfirmationResult::HashMismatch : IpConfirmationResult::StorageFailure);
            if (verifyIpConfirmation(request_, receipt_reader_.bytes(), manifest_, generation_, {author_, 64}, {author_hash_, 32},
                                     receipt_crypto_, hash_, domain_storage_, domain_capacity_, expected_, proof_) != ReceiptVerification::Valid)
                return stop(IpConfirmationResult::Conflict);
            stage_ = Stage::Persisting;
            return result_;
        }
        if (request_.manifest_transfer.size)
        {
            SignedManifestStore<Io> blobs(io_, reservation_a_, signed_path_);
            const auto restored = restoreIpPublicationHead(heads_, blobs, receipts_, manifest_, proof_, expected_, request_.network_request, request_.directory_public,
                                                           generation_, {author_, 64}, {author_hash_, 32}, head_, next_, head_verify_, scratch, capacity);
            if (restored != HeadStoreResult::Ready) return stop(restored == HeadStoreResult::StorageFailure ? IpConfirmationResult::StorageFailure : IpConfirmationResult::Conflict);
            result_ = IpConfirmationResult::Ready;
            wipe();
            return result_;
        }
        const auto reserved = reservations_.load(reservation_, reservation_verify_, scratch, capacity);
        if (reserved != ReservationStoreResult::Ready) return stop(reserved == ReservationStoreResult::StorageFailure ? IpConfirmationResult::StorageFailure : IpConfirmationResult::Conflict);
        const auto confirmed = confirmIpPublicationHead(heads_, receipts_, proof_, expected_, request_.network_request, request_.directory_public,
                                                        manifest_, reservation_, generation_, {author_, 64}, {author_hash_, 32}, head_, next_, head_verify_, scratch, capacity);
        if (confirmed != HeadStoreResult::Ready) return stop(confirmed == HeadStoreResult::StorageFailure ? IpConfirmationResult::StorageFailure : IpConfirmationResult::Conflict);
        result_ = IpConfirmationResult::Ready;
        wipe();
        return result_;
    }

  private:
    enum class Stage : uint8_t
    {
        Start,
        ImportReading,
        SignedReading,
        ReceiptReading,
        Persisting
    };
    void wipe()
    {
        std::memset(signed_storage_, 0, 4166);
        std::memset(receipt_storage_, 0, 1100);
        std::memset(domain_storage_, 0, domain_capacity_);
        manifest_ = {};
        proof_ = {};
    }
    IpConfirmationResult stop(IpConfirmationResult result)
    {
        signed_reader_.cancel();
        receipt_reader_.cancel();
        import_reader_.cancel();
        wipe();
        return result_ = result;
    }
    Io& io_;
    Hash& hash_;
    ManifestCrypto& manifest_crypto_;
    ReceiptCrypto& receipt_crypto_;
    uint8_t bindings_[352]{}, author_[64]{}, author_hash_[32]{};
    IpConfirmationRequest request_;
    uint32_t generation_ = 0;
    char head_a_[160]{}, head_b_[160]{}, reservation_a_[160]{}, reservation_b_[160]{}, signed_path_[160]{}, receipt_temporary_[160]{}, receipt_path_[160]{};
    char upload_path_[64]{}, upload_a_[64]{}, upload_b_[64]{};
    ObjectHead head_, next_, head_verify_;
    RevisionReservation reservation_, reservation_verify_;
    UploadJournal upload_, upload_verify_;
    PublicationJournal expected_;
    VerifiedManifestView manifest_;
    VerifiedReceiptView proof_;
    ObjectHeadStore<Io> heads_;
    ReservationStore<Io> reservations_;
    UploadJournalStore<Io> uploads_;
    SignedManifestStore<Io> receipts_;
    SignedManifestReader<Io> signed_reader_;
    UploadedManifestReader<Io, Hash, 1100> receipt_reader_;
    UploadedManifestReader<Io, Hash> import_reader_;
    uint8_t* signed_storage_;
    uint8_t* receipt_storage_;
    uint8_t* domain_storage_;
    size_t domain_capacity_;
    Stage stage_ = Stage::Start;
    bool valid_ = false;
    IpConfirmationResult result_ = IpConfirmationResult::Invalid;
};
} // namespace track_sharing
