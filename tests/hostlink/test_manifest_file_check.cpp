#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/manifest_file_check.h"
#include "track_sharing/prepared_signature.h"
#include <cassert>
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
        Read
    };
    struct OpenResult
    {
        void* handle;
    };
    std::vector<uint8_t> bytes;
    size_t offset = 0;
    bool open_ = false;
    OpenResult open(const char*, Mode)
    {
        assert(!open_);
        open_ = true;
        return {this};
    }
    bool close(void*)
    {
        open_ = false;
        return true;
    }
    bool size(void*, uint64_t& value)
    {
        value = bytes.size();
        return true;
    }
    bool seek(void*, uint64_t value)
    {
        offset = size_t(value);
        return offset <= bytes.size();
    }
    size_t read(void*, uint8_t* output, size_t length)
    {
        assert(length <= 320);
        std::memcpy(output, bytes.data() + offset, length);
        return length;
    }
};
static char names[2048];
static uint8_t scratch[320];
struct PreparedAdapter
{
    unsigned calls = 0;
    chat::DeviceAuthorResult signTrackManifest(const uint8_t*, size_t, uint32_t, track_sharing::ManifestView&, uint8_t*, size_t, uint8_t* signature, size_t)
    {
        ++calls;
        std::memset(signature, 7, 64);
        return chat::DeviceAuthorResult::Ok;
    }
};
struct PreparedBlobs
{
    bool fail = false;
    unsigned calls = 0;
    track_sharing::SignedStoreResult persist(geocaching::ByteView, geocaching::ByteView, uint8_t*, size_t)
    {
        ++calls;
        return fail ? track_sharing::SignedStoreResult::StorageFailure : track_sharing::SignedStoreResult::Ready;
    }
};
struct PreparedJournal
{
    bool fail = false;
    unsigned calls = 0;
    track_sharing::ReservationStoreResult validateCurrent(const track_sharing::RevisionReservation&, track_sharing::RevisionReservation&, uint8_t*, size_t)
    {
        return track_sharing::ReservationStoreResult::Ready;
    }
    track_sharing::ReservationStoreResult advance(const track_sharing::RevisionReservation& current, track_sharing::ReservationPhase phase, track_sharing::RevisionReservation& verify, uint8_t*, size_t)
    {
        ++calls;
        verify = current;
        verify.sequence++;
        verify.phase = phase;
        return fail ? track_sharing::ReservationStoreResult::StorageFailure : track_sharing::ReservationStoreResult::Ready;
    }
};
int main()
{
    Io io;
    const std::string xml = "<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document><description>" + std::string(400, 'x') + "</description><Placemark><LineString><coordinates>179,1 -179,-2</coordinates></LineString></Placemark></Document></kml>";
    io.bytes.assign(xml.begin(), xml.end());
    mbedtls_sha256_context context;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    uint8_t digest[32];
    assert(hash.start() && hash.update(io.bytes.data(), io.bytes.size()) && hash.finish(digest));
    track_sharing::VerifiedManifestView view;
    view.verified = true;
    auto& manifest = view.manifest;
    manifest.file_hash = {digest, 32};
    manifest.file_bytes = uint32_t(io.bytes.size());
    manifest.points = 2;
    manifest.west = 1790000000;
    manifest.east = -1790000000;
    manifest.south = -20000000;
    manifest.north = 10000000;
    using Check = track_sharing::ManifestFileCheck<Io, decltype(hash)>;
    using Result = track_sharing::ManifestFileResult;
    {
        Check refused(io, hash, names, sizeof(names));
        view.verified = false;
        assert(refused.begin("uploaded.kml", view) == Result::Invalid && !io.open_);
        view.verified = true;
    }
    static_assert(sizeof(Check) <= 4096, "Combined file check grew unbounded storage");
    {
        uint8_t raw[512], author[64] = {1}, nonce[16] = {2};
        geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
        assert(writer.array(14) && writer.unsignedInteger(1) && writer.binary({author, 64}) && writer.binary({nonce, 16}) && writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.nil() && writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.text("Track") && writer.text(""));
        assert(writer.array(4) && writer.signedInteger(manifest.west) && writer.signedInteger(manifest.south) && writer.signedInteger(manifest.east) && writer.signedInteger(manifest.north));
        assert(writer.array(4) && writer.unsignedInteger(0) && writer.nil() && writer.nil() && writer.unsignedInteger(2));
        assert(writer.array(4) && writer.binary({digest, 32}) && writer.unsignedInteger(io.bytes.size()) && writer.text("application/vnd.google-earth.kml+xml") && writer.unsignedInteger(1));
        track_sharing::ManifestView candidate;
        Check preflight(io, hash, names, sizeof(names));
        author[0] ^= 1;
        assert(preflight.beginCandidate("uploaded.kml", {raw, writer.size()}, {author, 64}, candidate) == Result::Invalid && !io.open_);
        author[0] ^= 1;
        assert(preflight.beginCandidate("uploaded.kml", {raw, writer.size() - 1}, {author, 64}, candidate) == Result::Invalid && !io.open_);
        track_sharing::UploadJournal upload;
        uint8_t author_hash[32];
        assert(hash.start() && hash.update(author, 64) && hash.finish(author_hash));
        upload.id[0] = 1;
        upload.sequence = 1;
        upload.generation = 7;
        upload.total = uint32_t(io.bytes.size());
        upload.durable_offset = upload.total;
        upload.phase = track_sharing::UploadPhase::Sealed;
        std::memcpy(upload.author_hash, author_hash, 32);
        std::memcpy(upload.file_hash, digest, 32);
        assert(preflight.beginUploadedCandidate("uploaded.kml", {raw, writer.size()}, {author, 64}, upload, 8, {author_hash, 32}, candidate) == Result::Invalid && !io.open_);
        upload.phase = track_sharing::UploadPhase::Checking;
        assert(preflight.beginUploadedCandidate("uploaded.kml", {raw, writer.size()}, {author, 64}, upload, 7, {author_hash, 32}, candidate) == Result::Invalid && !io.open_);
        upload.phase = track_sharing::UploadPhase::Sealed;
        upload.file_hash[31] ^= 1;
        assert(preflight.beginUploadedCandidate("uploaded.kml", {raw, writer.size()}, {author, 64}, upload, 7, {author_hash, 32}, candidate) == Result::Invalid && !io.open_);
        upload.file_hash[31] ^= 1;
        author_hash[31] ^= 1;
        assert(preflight.beginUploadedCandidate("uploaded.kml", {raw, writer.size()}, {author, 64}, upload, 7, {author_hash, 32}, candidate) == Result::Invalid && !io.open_);
        author_hash[31] ^= 1;
        assert(preflight.beginUploadedCandidate("uploaded.kml", {raw, writer.size()}, {author, 64}, upload, 7, {author_hash, 32}, candidate) == Result::Pending);
        assert(preflight.beginCandidate("uploaded.kml", {raw, writer.size()}, {author, 64}, candidate) == Result::Busy);
        Result result;
        do
        {
            result = preflight.step(scratch, sizeof(scratch));
            assert(!io.open_);
        } while (result == Result::Pending);
        assert(result == Result::Ready && candidate.author.size == 64);
        track_sharing::RevisionReservation reserved, verified;
        reserved.operation[0] = 1;
        reserved.generation = 7;
        reserved.revision = 1;
        reserved.sequence = 1;
        reserved.phase = track_sharing::ReservationPhase::Checked;
        std::memcpy(reserved.author, author_hash, 32);
        assert(track_sharing::deriveManifestHashes({raw, writer.size()}, candidate, hash, reserved.object, reserved.candidate));
        uint8_t workspace[4200], signature[64];
        PreparedAdapter adapter;
        PreparedBlobs blobs;
        PreparedJournal journal;
        using Prepared = track_sharing::PreparedSignatureResult;
        const auto reserved_before = reserved;
        assert(track_sharing::persistPreparedSignature(preflight, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, workspace, sizeof(workspace), reserved.operation, 64) == Prepared::Invalid);
        assert(!std::memcmp(&reserved, &reserved_before, sizeof(reserved)) && adapter.calls == 0 && blobs.calls == 0);
        assert(track_sharing::persistPreparedSignature(preflight, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, workspace, sizeof(workspace), author, 64) == Prepared::Invalid && author[0] == 1 && adapter.calls == 0);
        assert(track_sharing::persistPreparedSignature(preflight, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, reinterpret_cast<uint8_t*>(&candidate), sizeof(workspace), signature, sizeof(signature)) == Prepared::Invalid && candidate.author.data && adapter.calls == 0);
        Check unfinished(io, hash, names, sizeof(names));
        const size_t file_hash_offset = size_t(candidate.file_hash.data - raw);
        raw[file_hash_offset] ^= 1;
        assert(track_sharing::deriveManifestHashes({raw, writer.size()}, candidate, hash, reserved.object, reserved.candidate));
        assert(track_sharing::persistPreparedSignature(preflight, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, workspace, sizeof(workspace), signature, sizeof(signature)) == Prepared::Conflict && adapter.calls == 0 && blobs.calls == 0);
        raw[file_hash_offset] ^= 1;
        assert(track_sharing::deriveManifestHashes({raw, writer.size()}, candidate, hash, reserved.object, reserved.candidate));
        assert(track_sharing::persistPreparedSignature(unfinished, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, workspace, sizeof(workspace), signature, sizeof(signature)) == Prepared::NotChecked && adapter.calls == 0);
        blobs.fail = true;
        assert(track_sharing::persistPreparedSignature(preflight, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, workspace, sizeof(workspace), signature, sizeof(signature)) == Prepared::StorageFailure && journal.calls == 0);
        for (auto byte : signature) assert(!byte);
        blobs.fail = false;
        journal.fail = true;
        assert(track_sharing::persistPreparedSignature(preflight, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, workspace, sizeof(workspace), signature, sizeof(signature)) == Prepared::StorageFailure && reserved.phase == track_sharing::ReservationPhase::Checked);
        for (auto byte : signature) assert(!byte);
        journal.fail = false;
        assert(track_sharing::persistPreparedSignature(preflight, {raw, writer.size()}, {author, 64}, 7, {author_hash, 32}, reserved, verified, hash, adapter, blobs, journal, candidate, workspace, sizeof(workspace), signature, sizeof(signature)) == Prepared::Ready && reserved.phase == track_sharing::ReservationPhase::Signed);
        // A valid v2 document cannot become Ready from only a KML proof.
        std::vector<uint8_t> extended{0xdc, 0, 16, 2};
        extended.insert(extended.end(), raw + 2, raw + writer.size());
        const uint8_t tail[] = {0x98, 0, 0x90, 0, 0, 0, 0, 0, 0xc0, 0xc0};
        extended.insert(extended.end(), std::begin(tail), std::end(tail));
        track_sharing::ManifestView unsupported_view;
        Check unsupported(io, hash, names, sizeof(names));
        assert(unsupported.beginCandidate("uploaded.kml", {extended.data(), extended.size()}, {author, 64}, unsupported_view) == Result::Invalid);
        assert(unsupported_view.version == 2 && !unsupported.ready() && !io.open_);
    }
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        Check check(io, hash, names, sizeof(names));
        if (mode == 1) manifest.points = 3;
        else manifest.points = 2;
        if (mode == 2) digest[0] ^= 1;
        assert(check.begin("uploaded.kml", view) == Result::Pending);
        Result result;
        do
        {
            result = check.step(scratch, sizeof(scratch));
            assert(!io.open_);
        } while (result == Result::Pending);
        assert(result == (mode == 0 ? Result::Ready : mode == 1 ? Result::GeometryMismatch
                                                                : Result::HashMismatch));
        if (mode == 2) digest[0] ^= 1;
    }
    Check cancelled(io, hash, names, sizeof(names));
    assert(cancelled.begin("uploaded.kml", view) == Result::Pending && cancelled.cancel() == Result::Cancelled && !io.open_);
}
