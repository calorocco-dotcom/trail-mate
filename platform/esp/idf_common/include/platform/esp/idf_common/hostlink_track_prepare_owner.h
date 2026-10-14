#pragma once
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "platform/esp/idf_common/hostlink_track_attachment_prepare_owner.h"
#include "track_sharing/attachment_upload_bindings.h"
#include "track_sharing/bound_sealed_upload.h"
#include "track_sharing/prepare_upload_inputs.h"
#include "track_sharing/uploaded_candidate_check.h"
#include <esp_heap_caps.h>
#include <new>
namespace hostlink::management::prepare_runtime
{
namespace sd = platform::esp::arduino_common::storage;
// Input-validation stage only. Scheduler admits one PSRAM owner under an
// immutable upload/media lease; Ready is not signature/publication completion.
class TrackPrepareOwner
{
  public:
    TrackPrepareOwner(const track_sharing::PrepareRequest& source, uint32_t media_id, const uint8_t* public_key, const uint8_t* identity_hash)
        : media(media_id), generation(source.generation), file(sd::SdRuntimeMemoryPolicy::PsramOnly), io(file, media), hash(context),
          check(io, hash, manifest_upload, kml_upload, generation, author_hash, names, sizeof(names), view),
          binding_reader(io, hash, binding_upload, generation, author_hash)
    {
        std::memcpy(bindings, source.operation.data, 16);
        std::memcpy(bindings + 16, source.kml_upload.data, 16);
        std::memcpy(bindings + 32, source.manifest_upload.data, 16);
        request.operation = {bindings, 16};
        request.kml_upload = {bindings + 16, 16};
        request.manifest_upload = {bindings + 32, 16};
        request.generation = generation;
        request.package_request = source.package_request;
        if (source.package_upload.size)
        {
            std::memcpy(bindings + 80, source.package_upload.data, 16);
            request.package_upload = {bindings + 80, 16};
        }
        if (source.expected_parent.size)
        {
            std::memcpy(bindings + 48, source.expected_parent.data, 32);
            request.expected_parent = {bindings + 48, 32};
        }
        std::memcpy(author, public_key, 64);
        std::memcpy(author_hash, identity_hash, 32);
    }
    TrackPrepareOwner(const TrackPrepareOwner&) = delete;
    TrackPrepareOwner& operator=(const TrackPrepareOwner&) = delete;
    ~TrackPrepareOwner() { destroyAttachmentPrepareOwner(attachments); }
    track_sharing::ManifestFileResult start(uint8_t* scratch, size_t capacity)
    {
        if (state != track_sharing::ManifestFileResult::Idle) return track_sharing::ManifestFileResult::Busy;
        if (!mediaReady()) return state = track_sharing::ManifestFileResult::StorageFailure;
        const auto loaded = track_sharing::loadPrepareUploadInputs(io, request, generation, author_hash, kml_upload, manifest_upload, verify, kml_path, manifest_path, scratch, capacity);
        if (loaded != track_sharing::JournalStoreResult::Ready) return state = loaded == track_sharing::JournalStoreResult::StorageFailure ? track_sharing::ManifestFileResult::StorageFailure : track_sharing::ManifestFileResult::Invalid;
        const auto result = check.begin(request, manifest_path, kml_path, raw, sizeof(raw), {author, 64});
        return state = result;
    }
    track_sharing::ManifestFileResult step(uint32_t current_generation, const uint8_t* current_hash, uint8_t* scratch, size_t capacity)
    {
        if (!mediaReady())
        {
            cancelJobs();
            return state = track_sharing::ManifestFileResult::StorageFailure;
        }
        if (!current_hash || current_generation != generation || std::memcmp(current_hash, author_hash, 32))
        {
            cancelJobs();
            return state = track_sharing::ManifestFileResult::Invalid;
        }
        if (state != track_sharing::ManifestFileResult::Pending) return state;
        state = check.step(scratch, capacity, *this);
        if (state == track_sharing::ManifestFileResult::Ready && request.package_request && view.version != 2)
            state = track_sharing::ManifestFileResult::Invalid;
        return state;
    }
    track_sharing::ManifestFileResult checkAttachments(const track_sharing::ManifestView& candidate, uint8_t* scratch, size_t capacity)
    {
        using namespace track_sharing;
        if (!request.package_request || !getManifestExtensions(candidate, extensions)) return ManifestFileResult::Invalid;
        if (!extensions.assets.count) return request.package_upload.size ? ManifestFileResult::Invalid : ManifestFileResult::Ready;
        if (!request.package_upload.data || request.package_upload.size != 16) return ManifestFileResult::Invalid;
        if (!binding_started)
        {
            const auto loaded = loadBoundSealedUpload(io, request.package_upload, UploadKind::AttachmentBindings, generation, author_hash,
                                                      binding_upload, binding_verify, binding_path, scratch, capacity);
            if (loaded != JournalStoreResult::Ready) return loaded == JournalStoreResult::StorageFailure ? ManifestFileResult::StorageFailure : ManifestFileResult::Invalid;
            if (binding_reader.begin(binding_path, binding_raw, sizeof(binding_raw)) != FileIntegrityResult::Pending) return ManifestFileResult::Invalid;
            binding_started = true;
            return ManifestFileResult::Pending;
        }
        if (!attachments)
        {
            const auto read = binding_reader.step(scratch, capacity);
            if (read == FileIntegrityResult::Pending) return ManifestFileResult::Pending;
            if (read != FileIntegrityResult::Ready) return read == FileIntegrityResult::HashMismatch ? ManifestFileResult::HashMismatch : ManifestFileResult::StorageFailure;
            if (!decodeAttachmentUploadBindings(binding_reader.bytes(), extensions.assets.count, attachment_bindings)) return ManifestFileResult::Invalid;
            attachments = createAttachmentPrepareOwner(media, generation, author_hash, attachment_bindings.index_id.data, attachment_bindings.ids,
                                                       extensions.assets, candidate.file_hash, candidate.file_bytes);
            if (!attachments) return ManifestFileResult::Pending;
            const auto begun = attachments->begin();
            if (begun != AttachmentPackageResult::Pending) return begun == AttachmentPackageResult::StorageFailure ? ManifestFileResult::StorageFailure : ManifestFileResult::Invalid;
        }
        const auto result = attachments->step(generation, author_hash);
        return result == AttachmentPackageResult::Pending ? ManifestFileResult::Pending : result == AttachmentPackageResult::Ready        ? ManifestFileResult::Ready
                                                                                      : result == AttachmentPackageResult::HashMismatch   ? ManifestFileResult::HashMismatch
                                                                                      : result == AttachmentPackageResult::StorageFailure ? ManifestFileResult::StorageFailure
                                                                                                                                          : ManifestFileResult::Invalid;
    }
    bool matchesAttachments(const track_sharing::ManifestView& candidate) const
    {
        if (!request.package_request || candidate.version != 2 || !track_sharing::getManifestExtensions(candidate, extensions)) return false;
        return extensions.assets.count ? attachments && attachments->matches(extensions.assets, candidate.file_hash, candidate.file_bytes) : !request.package_upload.size && !attachments;
    }
    void cancel()
    {
        cancelJobs();
        state = track_sharing::ManifestFileResult::Cancelled;
    }
    bool ready() const { return state == track_sharing::ManifestFileResult::Ready && mediaReady() && check.ready(); }
    bool matchesCandidate(const track_sharing::ManifestView& candidate) const { return ready() && check.matchesCandidate(candidate) && (candidate.version == 1 || matchesAttachments(candidate)); }
    geocaching::ByteView manifest() const { return ready() ? check.manifest() : geocaching::ByteView{}; }
    bool mediaReady() const { return media == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active(); }
    uint32_t media, generation;
    uint8_t bindings[96]{}, author[64]{}, author_hash[32]{};
    track_sharing::PrepareRequest request;
    track_sharing::UploadJournal kml_upload, manifest_upload, verify;
    track_sharing::ManifestView view;
    char kml_path[64]{}, manifest_path[64]{}, names[2048]{};
    uint8_t raw[4096]{};
    mbedtls_sha256_context context;
    sd::SdRuntimeFile file;
    sd::SdBorrowedRecordFileIo io;
    platform::esp::storage::SnapshotStreamSha256 hash;
    TrackAttachmentPrepareOwner* attachments = nullptr;
    mutable track_sharing::ManifestExtensionsView extensions;

  private:
    track_sharing::UploadedCandidateCheck<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256> check;
    track_sharing::UploadJournal binding_upload, binding_verify;
    uint8_t binding_raw[1100]{};
    char binding_path[64]{};
    track_sharing::AttachmentUploadBindingsView attachment_bindings;
    track_sharing::UploadedManifestReader<sd::SdBorrowedRecordFileIo, platform::esp::storage::SnapshotStreamSha256, 1100> binding_reader;
    bool binding_started = false;
    void cancelJobs()
    {
        check.cancel();
        binding_reader.cancel();
        if (attachments) attachments->cancel();
    }
    track_sharing::ManifestFileResult state = track_sharing::ManifestFileResult::Idle;
};
static_assert(sizeof(TrackPrepareOwner) <= 12288, "Preparation input owner exceeds PSRAM admission");
inline TrackPrepareOwner* createPrepareOwner(const track_sharing::PrepareRequest& request, uint32_t media, const uint8_t* author, const uint8_t* author_hash)
{
    if (request.install_request || request.query_request || !author || !author_hash || !request.generation || !request.operation.data || request.operation.size != 16 || !request.kml_upload.data || request.kml_upload.size != 16 ||
        !request.manifest_upload.data || request.manifest_upload.size != 16 || (request.expected_parent.size && (!request.expected_parent.data || request.expected_parent.size != 32)) ||
        (request.package_upload.size && (!request.package_request || !request.package_upload.data || request.package_upload.size != 16)) ||
        media != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return nullptr;
    void* memory = heap_caps_malloc(sizeof(TrackPrepareOwner), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return nullptr;
    auto* created = new (memory) TrackPrepareOwner(request, media, author, author_hash);
    if (!created->file.has_backing_storage())
    {
        created->~TrackPrepareOwner();
        heap_caps_free(memory);
        return nullptr;
    }
    return created;
}
inline void destroyPrepareOwner(TrackPrepareOwner* owner)
{
    if (!owner) return;
    owner->cancel();
    owner->~TrackPrepareOwner();
    heap_caps_free(owner);
}
} // namespace hostlink::management::prepare_runtime
