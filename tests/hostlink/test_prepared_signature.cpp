#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "platform/esp/common/storage/track_manifest_crypto.h"
#include "platform/esp/common/storage/track_receipt_crypto.h"
#include "track_sharing/checked_reservation.h"
#include "track_sharing/commit_publication_head.h"
#include "track_sharing/confirm_publication.h"
#include "track_sharing/create_reservation.h"
#include "track_sharing/current_parent_loader.h"
#include "track_sharing/device_signer.h"
#include "track_sharing/manifest_file_check.h"
#include "track_sharing/network_publish.h"
#include "track_sharing/object_head_store.h"
#include "track_sharing/object_paths.h"
#include "track_sharing/persist_publication_intent.h"
#include "track_sharing/persist_signed_head.h"
#include "track_sharing/prepared_signature.h"
#include "track_sharing/publication_intent.h"
#include "track_sharing/publication_network_session.h"
#include "track_sharing/publication_receipt_loader.h"
#include "track_sharing/publication_reply_receiver.h"
#include "track_sharing/publication_send_step.h"
#include "track_sharing/request_retry_window.h"
#include "track_sharing/restore_prepared_signature.h"
#include "track_sharing/signed_manifest_reader.h"
#include "track_sharing/signed_object_head.h"
#include "track_sharing/uploaded_candidate_check.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* pointer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(pointer);
    while (length--) *bytes++ = 0;
}
#endif
struct Io
{
    enum class Mode
    {
        Read,
        CreateTemporary
    };
    enum class OpenStatus
    {
        Ready,
        Missing
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    std::map<std::string, std::vector<uint8_t>> files;
    std::map<std::string, unsigned> writes;
    std::string path;
    size_t offset = 0;
    bool active = false, short_log = false;
    OpenResult open(const char* name, Mode mode)
    {
        assert(!active);
        path = name;
        offset = 0;
        if (mode == Mode::Read && !files.count(path)) return {nullptr, OpenStatus::Missing};
        if (mode == Mode::CreateTemporary) files[path].clear();
        active = true;
        return {this, OpenStatus::Ready};
    }
    bool close(void*)
    {
        active = false;
        return true;
    }
    bool size(void*, uint64_t& size)
    {
        size = files[path].size();
        return true;
    }
    bool seek(void*, uint64_t position)
    {
        offset = size_t(position);
        return offset <= files[path].size();
    }
    size_t read(void*, uint8_t* out, size_t size)
    {
        assert(size <= 320);
        size = std::min(size, files[path].size() - offset);
        std::memcpy(out, files[path].data() + offset, size);
        offset += size;
        return size;
    }
    size_t write(void*, const uint8_t* in, size_t size)
    {
        assert(size <= 320);
        ++writes[path];
        if (short_log && path == "a") size /= 2;
        files[path].resize(offset + size);
        std::memcpy(files[path].data() + offset, in, size);
        offset += size;
        return size;
    }
    bool sync(void*) { return true; }
    bool publish(const char* from, const char* to)
    {
        assert(!active);
        if (!files.count(from) || files.count(to)) return false;
        files[to] = std::move(files[from]);
        files.erase(from);
        return true;
    }
};
struct Adapter
{
    uint8_t public_key[64]{}, secret[64]{};
    unsigned calls = 0;
    bool isReady() const { return true; }
    bool identityStorageError() const { return false; }
    uint32_t authorGeneration() const { return 7; }
    void combinedPublicKey(uint8_t* out) const { std::memcpy(out, public_key, 64); }
    bool sign(const uint8_t* message, size_t size, uint8_t* signature)
    {
        ++calls;
        ed25519_sign(signature, message, size, public_key + 32, secret);
        return true;
    }
    chat::DeviceAuthorResult signTrackManifest(const uint8_t* raw, size_t size, uint32_t generation, track_sharing::ManifestView& view, uint8_t* workspace, size_t capacity, uint8_t* signature, size_t signature_capacity)
    {
        return track_sharing::signDeviceTrackManifest(*this, raw, size, generation, view, workspace, capacity, signature, signature_capacity);
    }
};
int main()
{
    using namespace track_sharing;
    Io io;
    Adapter adapter;
    uint8_t seed[32] = {1};
    std::memset(adapter.public_key, 3, 32);
    ed25519_create_keypair(adapter.public_key + 32, adapter.secret, seed);
    const std::string xml = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></kml>";
    io.files["data"] = {xml.begin(), xml.end()};
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    uint8_t file_hash[32], author_hash[32], raw[512], workspace[4200], signature[64], nonce[16] = {1};
    char names[2048];
    assert(hash.start() && hash.update(io.files["data"].data(), xml.size()) && hash.finish(file_hash));
    assert(hash.start() && hash.update(adapter.public_key, 64) && hash.finish(author_hash));
    geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
    assert(writer.array(14) && writer.unsignedInteger(1) && writer.binary({adapter.public_key, 64}) && writer.binary({nonce, 16}) && writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.nil() && writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.text("Track") && writer.text(""));
    assert(writer.array(4) && writer.unsignedInteger(10000000) && writer.unsignedInteger(20000000) && writer.unsignedInteger(10000000) && writer.unsignedInteger(20000000));
    assert(writer.array(4) && writer.unsignedInteger(0) && writer.nil() && writer.nil() && writer.unsignedInteger(1));
    assert(writer.array(4) && writer.binary({file_hash, 32}) && writer.unsignedInteger(xml.size()) && writer.text("application/vnd.google-earth.kml+xml") && writer.unsignedInteger(1));
    geocaching::ByteView manifest{raw, writer.size()};
    const geocaching::ByteView author{adapter.public_key, 64};
    ManifestView view;
    RevisionReservation current, verify;
    uint8_t operation[16] = {1};
    assert(!createCandidateReservation(manifest, author, 0, {author_hash, 32}, {operation, 16}, nullptr, hash, view, current));
    assert(createCandidateReservation(manifest, author, 7, {author_hash, 32}, {operation, 16}, nullptr, hash, view, current) && current.phase == ReservationPhase::Reserved && current.revision == 1 && current.sequence == 1);
    ReservationStore<Io> journal(io, "a", "b");
    UploadJournal upload;
    upload.id[0] = 2;
    upload.sequence = 1;
    upload.generation = 7;
    upload.total = uint32_t(xml.size());
    upload.durable_offset = upload.total;
    upload.phase = UploadPhase::Sealed;
    std::memcpy(upload.author_hash, author_hash, 32);
    std::memcpy(upload.file_hash, file_hash, 32);
    io.files["manifest"] = {raw, raw + writer.size()};
    UploadJournal manifest_upload = upload;
    manifest_upload.id[0] = 3;
    manifest_upload.total = manifest_upload.durable_offset = uint32_t(writer.size());
    assert(hash.start() && hash.update(raw, writer.size()) && hash.finish(manifest_upload.file_hash));
    uint32_t live_generation = 7;
    uint8_t loaded[512];
    PrepareRequest request;
    request.operation = {operation, 16};
    request.kml_upload = {upload.id, 16};
    request.manifest_upload = {manifest_upload.id, 16};
    request.generation = 7;
    for (const char* damaged : {"manifest", "data"})
    {
        io.files[damaged][10] ^= 1;
        UploadedCandidateCheck<Io, decltype(hash)> rejected(io, hash, manifest_upload, upload, live_generation, author_hash, names, sizeof(names), view);
        assert(rejected.begin(request, "manifest", "data", loaded, sizeof(loaded), author) == ManifestFileResult::Pending);
        ManifestFileResult failed;
        do
        {
            failed = rejected.step(workspace, 320);
            assert(!io.active);
        } while (failed == ManifestFileResult::Pending);
        assert(failed != ManifestFileResult::Ready && !rejected.manifest().size && adapter.calls == 0 && !io.files.count("signed"));
        io.files[damaged][10] ^= 1;
    }
    UploadedCandidateCheck<Io, decltype(hash)> check(io, hash, manifest_upload, upload, live_generation, author_hash, names, sizeof(names), view);
    static_assert(sizeof(check) <= 4096, "Combined checker must borrow manifest and names storage");
    request.generation = 8;
    assert(check.begin(request, "manifest", "data", loaded, sizeof(loaded), author) == ManifestFileResult::Invalid && !io.active && adapter.calls == 0);
    request.generation = 7;
    request.kml_upload = {manifest_upload.id, 16};
    assert(check.begin(request, "manifest", "data", loaded, sizeof(loaded), author) == ManifestFileResult::Invalid && !io.active && adapter.calls == 0);
    request.kml_upload = {upload.id, 16};
    {
        request.expected_parent = {file_hash, 32};
        UploadedCandidateCheck<Io, decltype(hash)> wrong_parent(io, hash, manifest_upload, upload, live_generation, author_hash, names, sizeof(names), view);
        assert(wrong_parent.begin(request, "manifest", "data", loaded, sizeof(loaded), author) == ManifestFileResult::Pending);
        ManifestFileResult failed;
        do
        {
            failed = wrong_parent.step(workspace, 320);
        } while (failed == ManifestFileResult::Pending);
        assert(failed == ManifestFileResult::Invalid && !wrong_parent.manifest().size && !io.active && adapter.calls == 0);
        request.expected_parent = {};
    }
    assert(check.begin(request, "manifest", "data", loaded, sizeof(loaded), author) == ManifestFileResult::Pending);
    ManifestFileResult checked;
    do
    {
        checked = check.step(workspace, 320);
        assert(!io.active);
    } while (checked == ManifestFileResult::Pending);
    assert(checked == ManifestFileResult::Ready);
    manifest = check.manifest();
    assert(manifest.data == loaded && manifest.size == writer.size() && !std::memcmp(loaded, raw, manifest.size));
    RevisionReservation prepared_candidate;
    assert(persistCheckedReservation(check, request, manifest, author, {author_hash, 32}, 7, nullptr, nullptr, hash, journal, view, prepared_candidate, current, verify, workspace, sizeof(workspace)) == ReservationStoreResult::Ready && current.phase == ReservationPhase::Checked);
    const auto checked_files = io.files;
    const auto checked_writes = io.writes;
    assert(persistCheckedReservation(check, request, manifest, author, {author_hash, 32}, 7, nullptr, nullptr, hash, journal, view, prepared_candidate, current, verify, workspace, sizeof(workspace)) == ReservationStoreResult::Ready && io.files == checked_files && io.writes == checked_writes);
    request.generation = 8;
    assert(persistCheckedReservation(check, request, manifest, author, {author_hash, 32}, 7, nullptr, nullptr, hash, journal, view, prepared_candidate, current, verify, workspace, sizeof(workspace)) == ReservationStoreResult::Invalid && io.files == checked_files);
    request.generation = 7;
    SignedManifestStore<Io> blobs(io, "signed.tmp", "signed");
    io.short_log = true;
    assert(persistPreparedSignature(check, manifest, author, 7, {author_hash, 32}, current, verify, hash, adapter, blobs, journal, view, workspace, sizeof(workspace), signature, sizeof(signature)) == PreparedSignatureResult::StorageFailure);
    for (auto byte : signature) assert(!byte);
    assert(io.files.count("signed") && current.phase == ReservationPhase::Checked && !io.active);
    const auto persisted = io.files["signed"];
    const auto blob_writes = io.writes["signed.tmp"];
    assert(journal.load(current, verify, workspace, sizeof(workspace)) == ReservationStoreResult::Ready && current.phase == ReservationPhase::Checked);
    io.short_log = false;
    assert(persistPreparedSignature(check, manifest, author, 7, {author_hash, 32}, current, verify, hash, adapter, blobs, journal, view, workspace, sizeof(workspace), signature, sizeof(signature)) == PreparedSignatureResult::Ready);
    assert(current.phase == ReservationPhase::Signed && io.files["signed"] == persisted && io.writes["signed.tmp"] == blob_writes && !io.active);
    platform::esp::storage::TrackManifestCrypto crypto;
    assert(crypto.verify({adapter.public_key + 32, 31}, {signature, 64}, {workspace, 32}) == ManifestVerification::InvalidSignature);
    assert(crypto.verify({adapter.public_key + 32, 32}, {signature, 63}, {workspace, 32}) == ManifestVerification::InvalidSignature);
    assert(crypto.verify({adapter.public_key + 32, 32}, {signature, 64}, {}) == ManifestVerification::InvalidSignature);
    VerifiedManifestView signed_view;
    uint8_t parent_bytes[4166];
    SignedManifestReader<Io> parent_reader(io);
    static_assert(sizeof(parent_reader) <= 48, "Signed reader must borrow content storage");
    assert(parent_reader.begin("signed", parent_bytes, sizeof(parent_bytes)) == FileIntegrityResult::Pending && !parent_reader.bytes().size);
    FileIntegrityResult parent_read;
    do
    {
        parent_read = parent_reader.step();
        assert(!io.active);
    } while (parent_read == FileIntegrityResult::Pending);
    assert(parent_read == FileIntegrityResult::Ready && parent_reader.bytes().size == io.files["signed"].size());
    assert(verifyManifest(parent_reader.bytes(), crypto, hash, workspace, sizeof(workspace), signed_view, author) == ManifestVerification::Valid);
    VerifiedManifestView rejected_signature;
    const auto signature_tail = parent_reader.bytes().size - 1;
    parent_bytes[signature_tail] ^= 1;
    assert(verifyManifest(parent_reader.bytes(), crypto, hash, workspace, sizeof(workspace), rejected_signature, author) == ManifestVerification::InvalidSignature && !rejected_signature.verified);
    parent_bytes[signature_tail] ^= 1;
    {
        SignedManifestReader<Io> resized(io);
        assert(resized.begin("signed", workspace, sizeof(workspace)) == FileIntegrityResult::Pending);
        io.files["signed"].push_back(0);
        assert(resized.step() == FileIntegrityResult::StorageFailure && !resized.bytes().size && !io.active);
        for (size_t i = 0; i < persisted.size(); ++i) assert(workspace[i] == 0);
        io.files["signed"].pop_back();
        SignedManifestReader<Io> cancelled(io);
        assert(cancelled.begin("signed", workspace, sizeof(workspace)) == FileIntegrityResult::Pending);
        assert(cancelled.cancel() == FileIntegrityResult::Cancelled && !cancelled.bytes().size && !io.active);
    }
    assert(!std::memcmp(signed_view.object_id, current.object, 32) && !std::memcmp(signed_view.revision_hash, current.candidate, 32));
    ObjectHead head, head_verify, head_next;
    assert(!createSignedObjectHead(signed_view, current, 8, author, {author_hash, 32}, nullptr, head) && !head.sequence);
    current.candidate[0] ^= 1;
    assert(!createSignedObjectHead(signed_view, current, 7, author, {author_hash, 32}, nullptr, head) && !head.sequence);
    current.candidate[0] ^= 1;
    assert(createSignedObjectHead(signed_view, current, 7, author, {author_hash, 32}, nullptr, head) && head.phase == HeadPhase::Signed);
    char head_a[kObjectPathCapacity], head_b[kObjectPathCapacity];
    assert(resolveObjectPath({head.object, 32}, ObjectFile::HeadA, {}, head_a, sizeof(head_a)) && resolveObjectPath({head.object, 32}, ObjectFile::HeadB, {}, head_b, sizeof(head_b)));
    ObjectHeadStore<Io> heads(io, head_a, head_b);
    assert(persistSignedObjectHead(heads, signed_view, current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready);
    assert(heads.load(head, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready && !std::memcmp(head.hash, signed_view.revision_hash, 32));
    const auto head_files = io.files;
    const auto head_writes = io.writes;
    const auto head_signing_calls = adapter.calls;
    assert(persistSignedObjectHead(heads, signed_view, current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready);
    assert(io.files == head_files && io.writes == head_writes && adapter.calls == head_signing_calls);
    current.operation[0] ^= 1;
    assert(persistSignedObjectHead(heads, signed_view, current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Conflict && io.files == head_files && io.writes == head_writes);
    current.operation[0] ^= 1;
    assert(!createSignedObjectHead(signed_view, current, 7, author, {author_hash, 32}, &head, head_verify) && io.files == head_files);
    io.files[head_a][0] ^= 1;
    const auto corrupt_head_files = io.files;
    assert(persistSignedObjectHead(heads, signed_view, current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Corrupt);
    assert(io.files == corrupt_head_files && io.writes == head_writes && adapter.calls == head_signing_calls);
    io.files[head_a][0] ^= 1;
    const auto signing_calls = adapter.calls;
    assert(restorePreparedSignature(check, signed_view, author, 7, {author_hash, 32}, current, verify, journal, workspace, sizeof(workspace), signature, sizeof(signature)) == PreparedSignatureResult::Ready && adapter.calls == signing_calls);
    io.files["a"][0] ^= 1;
    assert(journal.load(current, verify, workspace, sizeof(workspace)) == ReservationStoreResult::Ready && current.phase == ReservationPhase::Checked);
    assert(restorePreparedSignature(check, signed_view, author, 8, {author_hash, 32}, current, verify, journal, workspace, sizeof(workspace), signature, sizeof(signature)) == PreparedSignatureResult::StaleIdentity);
    for (auto byte : signature) assert(!byte);
    assert(restorePreparedSignature(check, signed_view, author, 7, {author_hash, 32}, current, verify, journal, workspace, sizeof(workspace), signature, sizeof(signature)) == PreparedSignatureResult::Ready && current.phase == ReservationPhase::Signed && adapter.calls == signing_calls);
    assert(io.files["signed"] == persisted && !std::memcmp(signature, signed_view.signature.data, 64));
    // Fixture establishes local task admission; this is not a real network job.
    assert(heads.load(head, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready);
    head_next = head;
    ++head_next.sequence;
    head_next.phase = HeadPhase::Committed;
    assert(heads.advance(head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready);
    head = head_next;
    char canonical_parent[kObjectPathCapacity];
    assert(resolveObjectPath({head.object, 32}, ObjectFile::SignedManifest, {head.hash, 32}, canonical_parent, sizeof(canonical_parent)));
    // Fixture places the same immutable signed bytes under device-derived path.
    io.files[canonical_parent] = persisted;
    uint8_t loaded_parent[4166];
    VerifiedManifestView verified_parent;
    {
        const auto stable = io.files;
        uint8_t foreign_author[64];
        std::memcpy(foreign_author, author.data, 64);
        foreign_author[0] ^= 1;
        CurrentParentLoader<Io, decltype(hash), decltype(crypto), decltype(heads)> foreign(io, hash, crypto, heads, head, head_verify, verified_parent, canonical_parent, loaded_parent, sizeof(loaded_parent));
        assert(foreign.begin({current.object, 32}, {signed_view.revision_hash, 32}, {foreign_author, 64}, 8, workspace, sizeof(workspace)) == ParentLoadResult::Pending);
        ParentLoadResult status;
        do
        {
            status = foreign.step(workspace, sizeof(workspace));
        } while (status == ParentLoadResult::Pending);
        assert(status == ParentLoadResult::Invalid && !foreign.ready() && io.files == stable);
        CurrentParentLoader<Io, decltype(hash), decltype(crypto), decltype(heads)> restored(io, hash, crypto, heads, head, head_verify, verified_parent, canonical_parent, loaded_parent, sizeof(loaded_parent));
        assert(restored.begin({current.object, 32}, {signed_view.revision_hash, 32}, author, 8, workspace, sizeof(workspace)) == ParentLoadResult::Pending);
        do
        {
            status = restored.step(workspace, sizeof(workspace));
        } while (status == ParentLoadResult::Pending);
        assert(status == ParentLoadResult::Ready && restored.ready() && head.generation == 8 && head.revision == 1 && adapter.calls == signing_calls);
        assert(heads.load(head, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready && head.generation == 8);
        io.files = stable;
    }
    {
        const auto stable_head_files = io.files;
        CurrentParentLoader<Io, decltype(hash), decltype(crypto), decltype(heads)> stale_parent(io, hash, crypto, heads, head, head_verify, verified_parent, canonical_parent, loaded_parent, sizeof(loaded_parent));
        assert(stale_parent.begin({current.object, 32}, {signed_view.revision_hash, 32}, author, 7, workspace, sizeof(workspace)) == ParentLoadResult::Pending);
        head_next = head;
        ++head_next.sequence;
        assert(heads.advance(head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready);
        ParentLoadResult stale_result;
        do
        {
            stale_result = stale_parent.step(workspace, sizeof(workspace));
        } while (stale_result == ParentLoadResult::Pending);
        assert(stale_result == ParentLoadResult::Conflict && !stale_parent.ready() && adapter.calls == signing_calls);
        io.files = stable_head_files;
    }
    CurrentParentLoader<Io, decltype(hash), decltype(crypto), decltype(heads)> parent_loader(io, hash, crypto, heads, head, head_verify, verified_parent, canonical_parent, loaded_parent, sizeof(loaded_parent));
    assert(parent_loader.begin({current.object, 32}, {signed_view.revision_hash, 32}, author, 7, workspace, sizeof(workspace)) == ParentLoadResult::Pending);
    ParentLoadResult parent_status;
    do
    {
        parent_status = parent_loader.step(workspace, sizeof(workspace));
        assert(!io.active);
    } while (parent_status == ParentLoadResult::Pending);
    assert(parent_status == ParentLoadResult::Ready && parent_loader.ready() && verified_parent.manifest.revision == 1);
    geocaching::protocol::CmpWriter update_writer(raw, sizeof(raw));
    assert(update_writer.array(14) && update_writer.unsignedInteger(1) && update_writer.binary(author) && update_writer.binary({nonce, 16}) && update_writer.unsignedInteger(0) && update_writer.unsignedInteger(2) && update_writer.binary({signed_view.revision_hash, 32}) && update_writer.unsignedInteger(0) && update_writer.unsignedInteger(1) && update_writer.unsignedInteger(2) && update_writer.text("Updated") && update_writer.text(""));
    assert(update_writer.array(4) && update_writer.unsignedInteger(10000000) && update_writer.unsignedInteger(20000000) && update_writer.unsignedInteger(10000000) && update_writer.unsignedInteger(20000000));
    assert(update_writer.array(4) && update_writer.unsignedInteger(0) && update_writer.nil() && update_writer.nil() && update_writer.unsignedInteger(1));
    assert(update_writer.array(4) && update_writer.binary({file_hash, 32}) && update_writer.unsignedInteger(xml.size()) && update_writer.text("application/vnd.google-earth.kml+xml") && update_writer.unsignedInteger(1));
    io.files["manifest2"] = {raw, raw + update_writer.size()};
    manifest_upload.id[0] = 4;
    manifest_upload.total = manifest_upload.durable_offset = uint32_t(update_writer.size());
    assert(hash.start() && hash.update(raw, update_writer.size()) && hash.finish(manifest_upload.file_hash));
    uint8_t next_operation[16] = {2};
    request.operation = {next_operation, 16};
    request.expected_parent = {signed_view.revision_hash, 32};
    ManifestView update_view;
    UploadedCandidateCheck<Io, decltype(hash)> update_check(io, hash, manifest_upload, upload, live_generation, author_hash, names, sizeof(names), update_view);
    assert(update_check.begin(request, "manifest2", "data", loaded, sizeof(loaded), author) == ManifestFileResult::Pending);
    do
    {
        checked = update_check.step(workspace, 320);
    } while (checked == ManifestFileResult::Pending);
    assert(checked == ManifestFileResult::Ready);
    ReservationStore<Io> update_journal(io, "update.a", "update.b");
    RevisionReservation update_desired, update_current, update_verify;
    const auto before_update_reservation = io.files;
    request.expected_parent = {file_hash, 32};
    assert(persistCheckedReservation(update_check, request, update_check.manifest(), author, {author_hash, 32}, 7, &head, &verified_parent, hash, update_journal, update_view, update_desired, update_current, update_verify, workspace, sizeof(workspace)) == ReservationStoreResult::Conflict && io.files == before_update_reservation && adapter.calls == signing_calls);
    request.expected_parent = {signed_view.revision_hash, 32};
    assert(persistCheckedReservation(update_check, request, update_check.manifest(), author, {author_hash, 32}, 7, &head, &verified_parent, hash, update_journal, update_view, update_desired, update_current, update_verify, workspace, sizeof(workspace)) == ReservationStoreResult::Ready);
    SignedManifestStore<Io> update_blobs(io, "updated.tmp", "updated");
    assert(persistPreparedSignature(update_check, update_check.manifest(), author, 7, {author_hash, 32}, update_current, update_verify, hash, adapter, update_blobs, update_journal, update_view, workspace, sizeof(workspace), signature, sizeof(signature)) == PreparedSignatureResult::Ready);
    VerifiedManifestView verified_update;
    assert(verifyManifest({io.files["updated"].data(), io.files["updated"].size()}, crypto, hash, workspace, sizeof(workspace), verified_update, author) == ManifestVerification::Valid);
    assert(verified_update.manifest.revision == 2 && !std::memcmp(verified_update.manifest.previous.data, signed_view.revision_hash, 32));
    assert(persistSignedObjectHead(heads, verified_update, update_current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready);
    assert(head.revision == 2 && head.phase == HeadPhase::Signed && !std::memcmp(head.hash, verified_update.revision_hash, 32) && io.files["signed"] == persisted);
    FileIntegrityCheck<Io, decltype(hash)> retained_check(io, hash);
    assert(retained_check.begin("data", uint32_t(xml.size()), file_hash) == FileIntegrityResult::Pending);
    FileIntegrityResult retained_status;
    do
    {
        retained_status = retained_check.step(workspace, 320);
    } while (retained_status == FileIntegrityResult::Pending);
    assert(retained_status == FileIntegrityResult::Ready);
    uint8_t directory[16] = {8}, epoch[16] = {9};
    CommitRequest commit{{head.operation, 16}, {head.object, 32}, {head.hash, 32}, {directory, 16}, {epoch, 16}, 7, 2};
    PublicationJournal intent;
    assert(createPublicationIntent(commit, head, verified_update, update_current, author, {author_hash, 32}, retained_check, intent));
    assert(intent.sequence == 1 && intent.revision == 2 && intent.file_bytes == xml.size() && intent.phase == PublicationPhase::Accepted);
    PublicationJournalStore<Io> publication(io, "publish.a", "publish.b");
    PublicationJournal stored_job, job_verify;
    assert(persistPublicationIntent(publication, intent, stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
    auto progressed = stored_job;
    progressed.sequence = 2;
    progressed.phase = PublicationPhase::Uploading;
    progressed.offset = 1;
    assert(publication.advance(stored_job, progressed, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
    const auto admitted_files = io.files;
    assert(persistPublicationIntent(publication, intent, stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready && stored_job.sequence == 2 && stored_job.offset == 1 && io.files == admitted_files);
    auto wrong_target = intent;
    wrong_target.directory_epoch[0] ^= 1;
    assert(persistPublicationIntent(publication, wrong_target, stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Conflict && io.files == admitted_files);
    io.files["publish.a"][0] ^= 1;
    const auto damaged_job = io.files;
    assert(persistPublicationIntent(publication, intent, stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Corrupt && io.files == damaged_job);
    io.files = admitted_files;
    assert(verifyManifest({io.files["updated"].data(), io.files["updated"].size()}, crypto, hash, workspace, sizeof(workspace), verified_update, author) == ManifestVerification::Valid);
    assert(publication.load(stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
    assert(commitPublicationHead(heads, publication, stored_job, job_verify, verified_update, update_current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready);
    assert(head.phase == HeadPhase::Committed && head.revision == 2 && stored_job.phase == PublicationPhase::Uploading);
    const auto committed_job_files = io.files;
    assert(commitPublicationHead(heads, publication, stored_job, job_verify, verified_update, update_current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Ready && io.files == committed_job_files);
    io.files["publish.a"][0] ^= 1;
    const auto broken_admission = io.files;
    assert(commitPublicationHead(heads, publication, stored_job, job_verify, verified_update, update_current, 7, author, {author_hash, 32}, head, head_next, head_verify, workspace, sizeof(workspace)) == HeadStoreResult::Conflict && io.files == broken_admission);
    commit.generation = 8;
    assert(!createPublicationIntent(commit, head, verified_update, update_current, author, {author_hash, 32}, retained_check, intent) && !intent.sequence);
    {
        uint8_t receipt_raw[512], signed_receipt[1100], receipt_signature[64], directory_public[64]{}, directory_secret[64], directory_seed[32] = {17}, network_request[16] = {5};
        std::memcpy(network_request, stored_job.operation, 16);
        directory_public[0] = 3;
        ed25519_create_keypair(directory_public + 32, directory_secret, directory_seed);
        geocaching::protocol::CmpWriter receipt_writer(receipt_raw, sizeof(receipt_raw));
        assert(receipt_writer.array(13) && receipt_writer.unsignedInteger(1) && receipt_writer.binary({network_request, 16}) &&
               receipt_writer.binary({stored_job.object, 32}) && receipt_writer.binary({stored_job.revision_hash, 32}) && receipt_writer.unsignedInteger(stored_job.revision) &&
               receipt_writer.unsignedInteger(0) && receipt_writer.binary({stored_job.file_hash, 32}) && receipt_writer.unsignedInteger(stored_job.file_bytes) &&
               receipt_writer.binary({directory_public, 64}) && receipt_writer.binary({stored_job.directory, 16}) && receipt_writer.binary({stored_job.directory_epoch, 16}) &&
               receipt_writer.unsignedInteger(1) && receipt_writer.unsignedInteger(5000000000ULL));
        constexpr char receipt_domain[] = "TrailMate.Track.Receipt.v1";
        std::memcpy(workspace, receipt_domain, sizeof(receipt_domain));
        std::memcpy(workspace + sizeof(receipt_domain), receipt_raw, receipt_writer.size());
        ed25519_sign(receipt_signature, workspace, sizeof(receipt_domain) + receipt_writer.size(), directory_public + 32, directory_secret);
        geocaching::protocol::CmpWriter envelope(signed_receipt, sizeof(signed_receipt));
        assert(envelope.array(2) && envelope.binary({receipt_raw, receipt_writer.size()}) && envelope.binary({receipt_signature, 64}));
        uint8_t network_response[1200];
        geocaching::protocol::CmpWriter reply(network_response, sizeof(network_response));
        assert(reply.array(6) && reply.unsignedInteger(1) && reply.unsignedInteger(1) && reply.unsignedInteger(7) &&
               reply.binary({network_request, 16}) && reply.unsignedInteger(200) && reply.array(2) && reply.array(2) &&
               reply.binary({receipt_raw, receipt_writer.size()}) && reply.binary({receipt_signature, 64}) && reply.unsignedInteger(1));
        PublishResponseView publication_reply;
        assert(decodeTrackPublishResponse({network_response, reply.size()}, {stored_job.operation, 16}, publication_reply) && publication_reply.current);
        assert(publication_reply.signed_receipt.size == envelope.size() && !std::memcmp(publication_reply.signed_receipt.data, signed_receipt, envelope.size()));
        for (size_t length = 0; length < reply.size(); ++length)
            assert(!decodeTrackPublishResponse({network_response, length}, {stored_job.operation, 16}, publication_reply));
        assert(decodeTrackPublishResponse({network_response, reply.size()}, {stored_job.operation, 16}, publication_reply));
        platform::esp::storage::TrackReceiptCrypto receipt_crypto;
        VerifiedReceiptView proof;
        assert(verifyReceipt(publication_reply.signed_receipt, stored_job, {network_request, 16}, {directory_public, 64}, 0, receipt_crypto, workspace, sizeof(workspace), proof) == ReceiptVerification::Valid && proof.verified);
        io.files = committed_job_files;
        assert(publication.load(stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
        SignedManifestStore<Io> receipts(io, "receipt.tmp", "receipt.cmp");
        const auto before_confirmation = io.files;
        assert(confirmPublication(publication, receipts, proof, stored_job, progressed, job_verify, {network_request, 16}, {directory_public, 64}, 0, workspace, sizeof(workspace)) == PublicationStoreResult::Invalid && io.files == before_confirmation);
        progressed = stored_job;
        ++progressed.sequence;
        progressed.phase = PublicationPhase::FileStored;
        progressed.offset = progressed.file_bytes;
        assert(publication.advance(stored_job, progressed, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
        stored_job = progressed;
        const auto file_stored_files = io.files;
        assert(verifyManifest({io.files["updated"].data(), io.files["updated"].size()}, crypto, hash, workspace, sizeof(workspace), verified_update, author) == ManifestVerification::Valid);
        assert(markPublicationSending(publication, stored_job, progressed, job_verify, verified_update, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
        assert(stored_job.phase == PublicationPhase::Publishing);
        const auto publishing_files = io.files;
        assert(markPublicationSending(publication, stored_job, progressed, job_verify, verified_update, workspace, sizeof(workspace)) == PublicationStoreResult::Ready && io.files == publishing_files);
        uint8_t publish_request[4500];
        size_t publish_bytes = 0;
        assert(encodeTrackPublishRequest(stored_job, verified_update, publish_request, sizeof(publish_request), publish_bytes));
        geocaching::protocol::CmpReader publish_reader({publish_request, publish_bytes});
        size_t fields = 0;
        uint64_t number = 0;
        geocaching::ByteView field;
        assert(publish_reader.array(fields, 6) && fields == 6 && publish_reader.unsignedInteger(number) && number == 1 &&
               publish_reader.unsignedInteger(number) && number == 0 && publish_reader.unsignedInteger(number) && number == 7 &&
               publish_reader.binary(field, 16) && field.size == 16 && !std::memcmp(field.data, stored_job.operation, 16) &&
               publish_reader.unsignedInteger(number) && number == 8192 && publish_reader.array(fields, 2) && fields == 2 &&
               publish_reader.array(fields, 2) && fields == 2 && publish_reader.binary(field, 4096) && field.size == verified_update.manifest.raw.size &&
               !std::memcmp(field.data, verified_update.manifest.raw.data, field.size) && publish_reader.binary(field, 64) && field.size == 64 &&
               !std::memcmp(field.data, verified_update.signature.data, 64) && publish_reader.binary(field, 32) && field.size == 32 &&
               !std::memcmp(field.data, verified_update.manifest.previous.data, 32) && publish_reader.finished());
        assert(confirmPublication(publication, receipts, proof, stored_job, progressed, job_verify, {network_request, 16}, {directory_public, 64}, 0, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
        stored_job = progressed;
        assert(stored_job.phase == PublicationPhase::Confirmed && io.files["receipt.cmp"] == std::vector<uint8_t>(signed_receipt, signed_receipt + envelope.size()));
        const auto confirmed_files = io.files;
        assert(confirmPublication(publication, receipts, proof, stored_job, progressed, job_verify, {network_request, 16}, {directory_public, 64}, 0, workspace, sizeof(workspace)) == PublicationStoreResult::Ready && io.files == confirmed_files);
        io.files = publishing_files;
        assert(publication.load(stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready && stored_job.phase == PublicationPhase::Publishing);
        assert(verifyManifest({io.files["updated"].data(), io.files["updated"].size()}, crypto, hash, workspace, sizeof(workspace), verified_update, author) == ManifestVerification::Valid);
        struct ReceiptGuard
        {
            decltype(receipts)& store;
            bool allowed = false;
            SignedStoreResult persist(geocaching::ByteView raw, geocaching::ByteView signature, uint8_t* scratch, size_t capacity)
            {
                return allowed ? store.persist(raw, signature, scratch, capacity) : SignedStoreResult::StorageFailure;
            }
        } receipt_guard{receipts, false};
        struct Lease
        {
            bool valid = true;
            bool holds(const PublicationJournal&) const { return valid; }
        } lease;
        uint8_t local[16], delivery_hash[32]{}, wrong_address[16]{};
        std::memset(local, 6, 16);
        PublicationReplyReceiver<decltype(publication), decltype(receipt_guard), decltype(receipt_crypto), Lease> receiver(
            publication, receipt_guard, receipt_crypto, lease, stored_job, progressed, job_verify, proof, {directory_public, 64}, local, 0, workspace, sizeof(workspace));
        // Borrowed references, views, address and callback binding only. Keep
        // this bound strict while accounting for 64-bit host pointer widths.
        constexpr size_t receiver_header_budget = 8 * sizeof(void*) + sizeof(geocaching::ByteView) +
                                                  sizeof(uint8_t*) + sizeof(size_t) + 16 + alignof(void*) +
                                                  sizeof(chat::lxmf::TrackDeliveryBinding);
        static_assert(sizeof(receiver) <= receiver_header_budget, "Publication receiver owns no response buffer");
        struct PublicationSender
        {
            Io& io;
            const uint8_t* expected_author;
            PublicationJournal& job;
            bool allowed = false;
            unsigned calls = 0;
            std::vector<uint8_t> bytes;
            chat::MeshSendResult sendTrackData(const uint8_t* directory, chat::lxmf::ByteSpan payload, const uint8_t* author, uint32_t generation, bool response, std::array<uint8_t, 32>*)
            {
                assert(!io.active && job.phase == PublicationPhase::Publishing && generation == job.generation && !response &&
                       !std::memcmp(directory, job.directory, 16) && !std::memcmp(author, expected_author, 64));
                ++calls;
                bytes.assign(payload.data, payload.data + payload.size);
                return allowed ? chat::MeshSendResult::success(1) : chat::MeshSendResult::fail(chat::MeshOperationFailure::Busy);
            }
        } sender{io, author.data, stored_job, false, 0, {}};
        auto send_publication = [&]
        { return sendTrackPublicationStep(publication, receiver, sender, stored_job, progressed, job_verify, verified_update, author.data,
                                          publish_request, sizeof(publish_request), workspace, sizeof(workspace)); };
        io.files = file_stored_files;
        assert(publication.load(stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready && stored_job.phase == PublicationPhase::FileStored);
        assert(verifyManifest({io.files["updated"].data(), io.files["updated"].size()}, crypto, hash, workspace, sizeof(workspace), verified_update, author) == ManifestVerification::Valid);
        assert(sendTrackPublicationStep(publication, receiver, sender, stored_job, progressed, job_verify, verified_update, author.data, publish_request, 1, workspace, sizeof(workspace)) == PublicationSendResult::Invalid && io.files == file_stored_files && !sender.calls);
        struct SendingStore
        {
            decltype(publication)& actual;
            PublicationStoreResult validateCurrent(const PublicationJournal& job, PublicationJournal& verify, uint8_t* scratch, size_t capacity)
            {
                return actual.validateCurrent(job, verify, scratch, capacity);
            }
            PublicationStoreResult advance(const PublicationJournal&, const PublicationJournal&, PublicationJournal&, uint8_t*, size_t)
            {
                return PublicationStoreResult::StorageFailure;
            }
        } unavailable_store{publication};
        assert(sendTrackPublicationStep(unavailable_store, receiver, sender, stored_job, progressed, job_verify, verified_update, author.data, publish_request, sizeof(publish_request), workspace, sizeof(workspace)) == PublicationSendResult::StorageFailure && !receiver.pending() && !sender.calls && io.files == file_stored_files && stored_job.phase == PublicationPhase::FileStored);
        RequestRetryWindow retry;
        assert(retry.begin({stored_job.operation, 16}, 100, 1000, 10, 3) && retry.poll(receiver, 100) == ExchangePoll::Ready);
        assert(send_publication() == PublicationSendResult::Rejected && !receiver.pending() && sender.calls == 1 && io.files == publishing_files);
        assert(retry.sent(false, 100) && retry.poll(receiver, 109) == ExchangePoll::Waiting && sender.calls == 1);
        const auto rejected_bytes = sender.bytes;
        sender.allowed = true;
        assert(retry.poll(receiver, 110) == ExchangePoll::Ready);
        assert(send_publication() == PublicationSendResult::Queued && receiver.pending() && sender.calls == 2 && sender.bytes == rejected_bytes && io.files == publishing_files);
        assert(retry.sent(true, 110));
        assert(send_publication() == PublicationSendResult::Waiting && sender.calls == 2);
        assert(retry.poll(receiver, 1109) == ExchangePoll::Waiting && receiver.pending());
        assert(retry.poll(receiver, 1110) == ExchangePoll::Waiting && !receiver.pending());
        assert(retry.poll(receiver, 1120) == ExchangePoll::Ready && !std::memcmp(retry.request().data, stored_job.operation, 16));
        assert(send_publication() == PublicationSendResult::Queued && receiver.pending() && sender.calls == 3 && sender.bytes == rejected_bytes && io.files == publishing_files);
        assert(retry.sent(true, 1120));
        assert(!receiver.arm());
        chat::lxmf::CustomDeliveryView delivery{{stored_job.directory, 16}, {local, 16}, {delivery_hash, 32}, {network_response, reply.size()}};
        const auto receive = [&]
        { return chat::lxmf::deliverVerifiedTrack(&receiver.binding(), false, false, delivery); };
        const auto before_receive = io.files;
        lease.valid = false;
        assert(!receive() && receiver.pending() && io.files == before_receive);
        lease.valid = true;
        delivery.source = {wrong_address, 16};
        assert(!receive() && io.files == before_receive);
        delivery.source = {stored_job.directory, 16};
        delivery.destination = {wrong_address, 16};
        assert(!receive() && io.files == before_receive);
        delivery.destination = {local, 16};
        network_response[reply.size() - 2] ^= 1;
        assert(!receive() && receiver.pending() && !proof.verified && io.files == before_receive);
        network_response[reply.size() - 2] ^= 1;
        assert(!receive() && receiver.pending() && !proof.verified && stored_job.phase == PublicationPhase::Publishing && io.files == before_receive);
        receipt_guard.allowed = true;
        assert(receive() && !receiver.pending() && !proof.verified && receiver.directoryCurrent() && stored_job.phase == PublicationPhase::Confirmed);
        assert(retry.poll(receiver, 1121) == ExchangePoll::Completed);
        assert(io.files["receipt.cmp"] == std::vector<uint8_t>(signed_receipt, signed_receipt + envelope.size()));
        PublicationDirectory pinned_directory;
        std::memcpy(pinned_directory.delivery, stored_job.directory, 16);
        std::memcpy(pinned_directory.epoch, stored_job.directory_epoch, 16);
        std::memcpy(pinned_directory.public_key, directory_public, 64);
        pinned_directory.file_limit = 8388608;
        pinned_directory.chunk_limit = 1024;
        pinned_directory.query_limit = 32;
        uint8_t recovered_bytes[1100];
        VerifiedReceiptView recovered_proof;
        PublicationReceiptLoader<Io, decltype(receipt_crypto)> loader(io, receipt_crypto, stored_job, pinned_directory, recovered_proof, 0);
        static_assert(sizeof(loader) <= 128, "Receipt reader borrows all byte storage");
        assert(loader.begin("receipt.cmp", recovered_bytes, sizeof(recovered_bytes)) == ReceiptLoadResult::Pending && !io.active);
        ReceiptLoadResult loaded = ReceiptLoadResult::Pending;
        for (unsigned attempt = 0; attempt < 8 && loaded == ReceiptLoadResult::Pending; ++attempt)
        {
            loaded = loader.step(workspace, sizeof(workspace));
            assert(!io.active);
        }
        assert(loaded == ReceiptLoadResult::Ready && loader.ready() && recovered_proof.verified);
        const auto durable_receipt = io.files["receipt.cmp"];
        // Receipt was saved, but the Confirmed journal write was interrupted.
        io.files = publishing_files;
        io.files["receipt.cmp"] = durable_receipt;
        assert(publication.load(stored_job, job_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready && stored_job.phase == PublicationPhase::Publishing);
        PublicationReceiptLoader<Io, decltype(receipt_crypto)> interrupted(io, receipt_crypto, stored_job, pinned_directory, recovered_proof, 0);
        assert(interrupted.begin("receipt.cmp", recovered_bytes, sizeof(recovered_bytes)) == ReceiptLoadResult::Pending);
        loaded = ReceiptLoadResult::Pending;
        for (unsigned attempt = 0; attempt < 8 && loaded == ReceiptLoadResult::Pending; ++attempt) loaded = interrupted.step(workspace, sizeof(workspace));
        assert(loaded == ReceiptLoadResult::Ready && interrupted.ready());
        assert(confirmPublication(publication, receipts, recovered_proof, stored_job, progressed, job_verify, {stored_job.operation, 16}, {directory_public, 64}, 0, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
        stored_job = progressed;
        assert(stored_job.phase == PublicationPhase::Confirmed);
        PublicationReceiptLoader<Io, decltype(receipt_crypto)> missing(io, receipt_crypto, stored_job, pinned_directory, recovered_proof, 0);
        assert(missing.begin("missing", recovered_bytes, sizeof(recovered_bytes)) == ReceiptLoadResult::Missing && !recovered_proof.verified);
        pinned_directory.public_key[0] ^= 1;
        PublicationReceiptLoader<Io, decltype(receipt_crypto)> wrong_key(io, receipt_crypto, stored_job, pinned_directory, recovered_proof, 0);
        assert(wrong_key.begin("receipt.cmp", recovered_bytes, sizeof(recovered_bytes)) == ReceiptLoadResult::Pending);
        loaded = ReceiptLoadResult::Pending;
        for (unsigned attempt = 0; attempt < 8 && loaded == ReceiptLoadResult::Pending; ++attempt) loaded = wrong_key.step(workspace, sizeof(workspace));
        assert(loaded == ReceiptLoadResult::Conflict && !wrong_key.ready() && !recovered_proof.verified);
        pinned_directory.public_key[0] ^= 1;
        io.files["receipt.cmp"].back() ^= 1;
        PublicationReceiptLoader<Io, decltype(receipt_crypto)> damaged(io, receipt_crypto, stored_job, pinned_directory, recovered_proof, 0);
        assert(damaged.begin("receipt.cmp", recovered_bytes, sizeof(recovered_bytes)) == ReceiptLoadResult::Pending);
        loaded = ReceiptLoadResult::Pending;
        for (unsigned attempt = 0; attempt < 8 && loaded == ReceiptLoadResult::Pending; ++attempt) loaded = damaged.step(workspace, sizeof(workspace));
        assert(loaded == ReceiptLoadResult::Conflict && !damaged.ready() && !recovered_proof.verified);
        io.files["receipt.cmp"] = durable_receipt;
        {
            Io network_io;
            network_io.files = io.files;
            PublicationJournal network_job, network_next, network_verify;
            VerifiedManifestView network_manifest;
            VerifiedReceiptView network_proof;
            assert(verifyManifest({network_io.files["updated"].data(), network_io.files["updated"].size()}, crypto, hash, workspace, sizeof(workspace), network_manifest, author) == ManifestVerification::Valid);
            PublicationJournalStore<Io> network_jobs(network_io, "network.a", "network.b");
            auto network_intent = stored_job;
            network_intent.sequence = 1;
            network_intent.offset = 0;
            network_intent.phase = PublicationPhase::Accepted;
            assert(persistPublicationIntent(network_jobs, network_intent, network_job, network_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
            const auto initial_network_files = network_io.files;
            SignedManifestStore<Io> network_receipts(network_io, "network-receipt.tmp", "network-receipt.cmp");
            struct PacketSender
            {
                Io& io;
                unsigned calls = 0;
                std::vector<uint8_t> packet;
                chat::MeshSendResult sendTrackData(const uint8_t*, chat::lxmf::ByteSpan bytes, const uint8_t*, uint32_t, bool, std::array<uint8_t, 32>*)
                {
                    assert(!io.active);
                    ++calls;
                    packet.assign(bytes.data, bytes.data + bytes.size);
                    return chat::MeshSendResult::success(1);
                }
            } packet_sender{network_io, 0, {}};
            struct Nonce
            {
                uint8_t counter = 20;
                bool fill(uint8_t* output, size_t size)
                {
                    assert(size == 16);
                    std::memset(output, 0, size);
                    output[0] = counter++;
                    return true;
                }
            } nonce;
            struct BackendIdentity
            {
                geocaching::ByteView key;
                uint32_t generation = 7;
                bool isReady() const { return true; }
                bool identityStorageError() const { return false; }
                uint32_t authorGeneration() const { return generation; }
                const uint8_t* encryptionPublicKey() const { return key.data; }
                const uint8_t* signingPublicKey() const { return key.data + 32; }
            } backend_identity{author, 7};
            uint8_t network_wire[4300], network_scratch[1100], file_scratch[256], response_wire[512], upload_token[16] = {33};
            PublicationNetworkSession<Io, decltype(network_jobs), decltype(network_receipts), decltype(receipt_crypto), Lease, PacketSender, Nonce> execution(
                network_io, network_jobs, network_receipts, receipt_crypto, lease, packet_sender, nonce, network_job, network_next, network_verify,
                network_manifest, network_proof, {directory_public, 64}, local, author.data, "data", network_wire, sizeof(network_wire), network_scratch, sizeof(network_scratch),
                file_scratch, sizeof(file_scratch), 20, 5, 3);
            static_assert(sizeof(execution) <= 768, "Network execution owns bounded metadata only");
            lease.valid = false;
            assert(execution.advance(0) == NetworkSessionResult::Conflict && !packet_sender.calls);
            lease.valid = true;
            bool lost_begin = false, confirmed = false;
            std::vector<uint8_t> original_begin;
            for (uint64_t now = 0; now < 1000 && !confirmed; now += 10)
            {
                const auto step = execution.advance(now);
                if (step == NetworkSessionResult::Confirmed)
                {
                    confirmed = true;
                    break;
                }
                if (step == NetworkSessionResult::Waiting || step == NetworkSessionResult::Progress) continue;
                assert(step == NetworkSessionResult::Queued);
                geocaching::protocol::CmpReader packet({packet_sender.packet.data(), packet_sender.packet.size()});
                size_t count = 0;
                uint64_t version = 0, operation = 0, budget = 0, offset = 0;
                geocaching::ByteView request_id, field;
                assert(packet.array(count, 6) && count == 6 && packet.unsignedInteger(version) && version == 1 && packet.unsignedInteger(version) && version == 0 &&
                       packet.unsignedInteger(operation) && packet.binary(request_id, 16) && packet.unsignedInteger(budget) && budget == 8192 && packet.array(count, 3));
                if (operation == 4 && !lost_begin)
                {
                    lost_begin = true;
                    original_begin = packet_sender.packet;
                    continue;
                }
                geocaching::protocol::CmpWriter response_writer(response_wire, sizeof(response_wire));
                assert(response_writer.array(6) && response_writer.unsignedInteger(1) && response_writer.unsignedInteger(1) && response_writer.unsignedInteger(operation) &&
                       response_writer.binary(request_id) && response_writer.unsignedInteger(200));
                if (operation == 4)
                {
                    assert(packet_sender.packet == original_begin && count == 2 && packet.binary(field, 32) && !std::memcmp(field.data, network_job.file_hash, 32) &&
                           packet.unsignedInteger(offset) && offset == network_job.file_bytes && packet.finished());
                    assert(response_writer.array(4) && response_writer.binary({upload_token, 16}) && response_writer.unsignedInteger(0) &&
                           response_writer.unsignedInteger(0) && response_writer.unsignedInteger(256));
                }
                else if (operation == 5)
                {
                    assert(request_id.data[0] != 20);
                    assert(count == 3 && packet.binary(field, 16) && !std::memcmp(field.data, upload_token, 16) && packet.unsignedInteger(offset) &&
                           packet.binary(field, 256) && packet.finished() && offset == network_job.offset &&
                           !std::memcmp(field.data, network_io.files["data"].data() + offset, field.size));
                    assert(response_writer.array(2) && response_writer.binary({upload_token, 16}) && response_writer.unsignedInteger(offset + field.size));
                }
                else if (operation == 6)
                {
                    assert(count == 1 && packet.binary(field, 16) && !std::memcmp(field.data, upload_token, 16) && packet.finished());
                    assert(response_writer.array(4) && response_writer.binary({network_job.file_hash, 32}) && response_writer.unsignedInteger(network_job.file_bytes) &&
                           response_writer.unsignedInteger(1) && response_writer.unsignedInteger(0));
                }
                else assert(operation == 7 && count == 2 && network_job.phase == PublicationPhase::Publishing && !std::memcmp(request_id.data, network_job.operation, 16));
                chat::lxmf::CustomDeliveryView incoming{{network_job.directory, 16}, {local, 16}, {delivery_hash, 32}, operation == 7 ? chat::lxmf::ByteSpan{network_response, reply.size()} : chat::lxmf::ByteSpan{response_wire, response_writer.size()}};
                const auto sequence_before_reply = network_job.sequence;
                assert(!chat::lxmf::deliverVerifiedTrackForAuthor(&execution.binding(), true, false, incoming, backend_identity) && network_job.sequence == sequence_before_reply);
                incoming.destination = {wrong_address, 16};
                assert(!chat::lxmf::deliverVerifiedTrackForAuthor(&execution.binding(), false, false, incoming, backend_identity) && network_job.sequence == sequence_before_reply);
                incoming.destination = {local, 16};
                ++backend_identity.generation;
                assert(!chat::lxmf::deliverVerifiedTrackForAuthor(&execution.binding(), false, false, incoming, backend_identity) && network_job.sequence == sequence_before_reply);
                --backend_identity.generation;
                assert(chat::lxmf::deliverVerifiedTrackForAuthor(&execution.binding(), false, false, incoming, backend_identity));
                if (operation == 4)
                {
                    const auto calls_before_pause = packet_sender.calls;
                    execution.pause();
                    assert(execution.advance(now + 1) == NetworkSessionResult::Paused && packet_sender.calls == calls_before_pause);
                    assert(execution.resume(now + 2));
                }
                assert(!network_io.active);
            }
            assert(confirmed && lost_begin && network_job.phase == PublicationPhase::Confirmed && execution.directoryCurrent() &&
                   network_io.files["network-receipt.cmp"] == durable_receipt);
            assert(network_jobs.load(network_job, network_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready && network_job.phase == PublicationPhase::Confirmed);
            uint8_t cancel_author[32];
            std::memcpy(cancel_author, network_job.author_hash, 32);
            assert(execution.cancel(network_job.generation, {cancel_author, 32}) == PublicationStoreResult::Conflict);
            network_io.files = initial_network_files;
            assert(network_jobs.load(network_job, network_verify, workspace, sizeof(workspace)) == PublicationStoreResult::Ready);
            decltype(execution) cancelled_execution(network_io, network_jobs, network_receipts, receipt_crypto, lease, packet_sender, nonce, network_job, network_next, network_verify,
                                                    network_manifest, network_proof, {directory_public, 64}, local, author.data, "data", network_wire, sizeof(network_wire), network_scratch, sizeof(network_scratch),
                                                    file_scratch, sizeof(file_scratch), 20, 5, 3);
            assert(cancelled_execution.advance(1000) == NetworkSessionResult::Queued);
            geocaching::protocol::CmpReader cancelled_packet({packet_sender.packet.data(), packet_sender.packet.size()});
            size_t cancelled_fields = 0;
            uint64_t cancelled_number = 0;
            geocaching::ByteView cancelled_id;
            assert(cancelled_packet.array(cancelled_fields, 6) && cancelled_fields == 6 && cancelled_packet.unsignedInteger(cancelled_number) &&
                   cancelled_packet.unsignedInteger(cancelled_number) && cancelled_packet.unsignedInteger(cancelled_number) && cancelled_number == 4 && cancelled_packet.binary(cancelled_id, 16));
            geocaching::protocol::CmpWriter late(response_wire, sizeof(response_wire));
            assert(late.array(6) && late.unsignedInteger(1) && late.unsignedInteger(1) && late.unsignedInteger(4) && late.binary(cancelled_id) && late.unsignedInteger(200) &&
                   late.array(4) && late.nil() && late.unsignedInteger(network_job.file_bytes) && late.unsignedInteger(1) && late.unsignedInteger(1024));
            assert(cancelled_execution.cancel(network_job.generation, {cancel_author, 32}) == PublicationStoreResult::Ready && network_job.phase == PublicationPhase::Cancelled);
            const auto cancelled_sequence = network_job.sequence;
            const auto cancelled_sends = packet_sender.calls;
            uint8_t late_hash[32]{};
            chat::lxmf::CustomDeliveryView late_delivery{{network_job.directory, 16}, {local, 16}, {late_hash, 32}, {response_wire, late.size()}};
            chat::lxmf::deliverVerifiedTrackForAuthor(&cancelled_execution.binding(), false, false, late_delivery, backend_identity);
            assert(network_job.sequence == cancelled_sequence && network_job.phase == PublicationPhase::Cancelled && !cancelled_execution.resume(1001));
            assert(cancelled_execution.advance(1002) == NetworkSessionResult::Paused && packet_sender.calls == cancelled_sends);
        }
        PublicationReceiptLoader<Io, decltype(receipt_crypto)> cancelled(io, receipt_crypto, stored_job, pinned_directory, recovered_proof, 0);
        assert(cancelled.begin("receipt.cmp", recovered_bytes, sizeof(recovered_bytes)) == ReceiptLoadResult::Pending);
        assert(cancelled.step(workspace, sizeof(workspace)) == ReceiptLoadResult::Pending && !io.active);
        assert(cancelled.cancel() == ReceiptLoadResult::Cancelled && !cancelled.ready() && !recovered_proof.verified);
        for (size_t i = 0; i < durable_receipt.size(); ++i) assert(!recovered_bytes[i]);
        PublicationReceiptLoader<Io, decltype(receipt_crypto)> changed_file(io, receipt_crypto, stored_job, pinned_directory, recovered_proof, 0);
        assert(changed_file.begin("receipt.cmp", recovered_bytes, sizeof(recovered_bytes)) == ReceiptLoadResult::Pending);
        io.files["receipt.cmp"].pop_back();
        assert(changed_file.step(workspace, sizeof(workspace)) == ReceiptLoadResult::StorageFailure && !changed_file.ready() && !recovered_proof.verified && !io.active);
        io.files["receipt.cmp"] = durable_receipt;
        network_request[0] ^= 1;
        assert(verifyReceipt({signed_receipt, envelope.size()}, stored_job, {network_request, 16}, {directory_public, 64}, 0, receipt_crypto, workspace, sizeof(workspace), proof) == ReceiptVerification::BindingMismatch && !proof.verified);
        network_request[0] ^= 1;
        signed_receipt[envelope.size() - 1] ^= 1;
        assert(verifyReceipt({signed_receipt, envelope.size()}, stored_job, {network_request, 16}, {directory_public, 64}, 0, receipt_crypto, workspace, sizeof(workspace), proof) == ReceiptVerification::InvalidSignature && !proof.verified);
    }
}
