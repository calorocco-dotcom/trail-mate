#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "platform/esp/common/storage/track_manifest_crypto.h"
#include "platform/esp/common/storage/track_receipt_crypto.h"
#include "track_confirmation_memory_io.h"
#include "track_sharing/ip_confirmation_work.h"
#include "track_sharing/verify_ip_confirmation.h"
#include <cassert>
#include <memory>
#if defined(_WIN32)
extern "C" void mbedtls_platform_zeroize(void* buffer, size_t length)
{
    auto* bytes = static_cast<volatile uint8_t*>(buffer);
    while (length--) *bytes++ = 0;
}
#endif
static uint8_t author[64], author_hash[32], directory[64], secret[64], delivery[32], name_hash[32], id_hash[32], joined[26];
static uint8_t raw[1024], signed_bytes[1100], domain[1100], workspace[1200], signature[64], proof_hash[32], file_hash[32];
static uint8_t operation[16] = {1}, transfer[16] = {2}, epoch[16] = {3}, network_request[16] = {4};
static mbedtls_sha256_context context;
static uint8_t local_raw[4096], local_signed[4166], author_secret[64], work_signed[4166], work_receipt[1100], work_domain[4166], work_scratch[448];
int main()
{
    using namespace track_sharing;
    platform::esp::storage::SnapshotStreamSha256 hash(context);
    const auto digest = [&](geocaching::ByteView source, uint8_t* output)
    {
        assert(hash.start() && hash.update(source.data, source.size) && hash.finish(output));
        hash.reset();
    };
    std::memset(author, 7, 64);
    std::memset(directory, 8, 32);
    std::memset(file_hash, 9, 32);
    uint8_t seed[32] = {10};
    ed25519_create_keypair(directory + 32, secret, seed);
    seed[0] = 11;
    ed25519_create_keypair(author + 32, author_secret, seed);
    digest({author, 64}, author_hash);
    digest({directory, 64}, id_hash);
    constexpr char service[] = "lxmf.delivery";
    digest({reinterpret_cast<const uint8_t*>(service), sizeof(service) - 1}, name_hash);
    std::memcpy(joined, name_hash, 10);
    std::memcpy(joined + 10, id_hash, 16);
    digest({joined, 26}, delivery);
    VerifiedManifestView manifest;
    manifest.verified = true;
    manifest.manifest.author = {author, 64};
    manifest.manifest.revision = 1;
    manifest.manifest.file_hash = {file_hash, 32};
    manifest.manifest.file_bytes = 500;
    std::memset(manifest.object_id, 11, 32);
    std::memset(manifest.revision_hash, 12, 32);
    geocaching::protocol::CmpWriter local_writer(local_raw, sizeof(local_raw));
    assert(local_writer.array(14) && local_writer.unsignedInteger(1) && local_writer.binary({author, 64}) && local_writer.binary({transfer, 16}) &&
           local_writer.unsignedInteger(0) && local_writer.unsignedInteger(1) && local_writer.nil() && local_writer.unsignedInteger(0) &&
           local_writer.unsignedInteger(1000) && local_writer.unsignedInteger(1000) && local_writer.text("Hardware-independent confirmation") && local_writer.text(""));
    assert(local_writer.array(4));
    for (unsigned i = 0; i < 4; ++i) assert(local_writer.unsignedInteger(0));
    assert(local_writer.array(4) && local_writer.unsignedInteger(0) && local_writer.nil() && local_writer.nil() && local_writer.unsignedInteger(1) &&
           local_writer.array(4) && local_writer.binary({file_hash, 32}) && local_writer.unsignedInteger(500) &&
           local_writer.text("application/vnd.google-earth.kml+xml") && local_writer.unsignedInteger(1));
    constexpr char local_prefix[] = "TrailMate.Track.Manifest.v1";
    std::memcpy(domain, local_prefix, sizeof(local_prefix));
    std::memcpy(domain + sizeof(local_prefix), local_raw, local_writer.size());
    ed25519_sign(signature, domain, sizeof(local_prefix) + local_writer.size(), author + 32, author_secret);
    geocaching::protocol::CmpWriter local_envelope(local_signed, sizeof(local_signed));
    assert(local_envelope.array(2) && local_envelope.binary({local_raw, local_writer.size()}) && local_envelope.binary({signature, 64}));
    platform::esp::storage::TrackManifestCrypto manifest_crypto;
    assert(verifyManifest({local_signed, local_envelope.size()}, manifest_crypto, hash, work_domain, sizeof(work_domain), manifest, {author, 64}) == ManifestVerification::Valid);
    geocaching::protocol::CmpWriter writer(raw, sizeof(raw));
    assert(writer.array(13) && writer.unsignedInteger(1) && writer.binary({network_request, 16}) && writer.binary({manifest.object_id, 32}) &&
           writer.binary({manifest.revision_hash, 32}) && writer.unsignedInteger(1) && writer.unsignedInteger(0) && writer.binary({file_hash, 32}) &&
           writer.unsignedInteger(500) && writer.binary({directory, 64}) && writer.binary({delivery, 16}) && writer.binary({epoch, 16}) &&
           writer.unsignedInteger(1) && writer.unsignedInteger(5000000000ULL));
    const auto raw_size = writer.size();
    constexpr char prefix[] = "TrailMate.Track.Receipt.v1";
    std::memcpy(domain, prefix, sizeof(prefix));
    std::memcpy(domain + sizeof(prefix), raw, raw_size);
    ed25519_sign(signature, domain, sizeof(prefix) + raw_size, directory + 32, secret);
    geocaching::protocol::CmpWriter envelope(signed_bytes, sizeof(signed_bytes));
    assert(envelope.array(2) && envelope.binary({raw, raw_size}) && envelope.binary({signature, 64}));
    const geocaching::ByteView receipt{signed_bytes, envelope.size()};
    digest(receipt, proof_hash);
    IpConfirmationRequest request{{operation, 16}, {transfer, 16}, {proof_hash, 32}, {directory, 64}, {delivery, 16}, {epoch, 16}, {network_request, 16}, {manifest.object_id, 32}, {manifest.revision_hash, 32}, 1, 1};
    PublicationJournal expected;
    VerifiedReceiptView proof;
    platform::esp::storage::TrackReceiptCrypto crypto;
    const auto check = [&]
    { return verifyIpConfirmation(request, receipt, manifest, 1, {author, 64}, {author_hash, 32}, crypto, hash,
                                  workspace, sizeof(workspace), expected, proof); };
    assert(check() == ReceiptVerification::Valid && proof.verified && publicationJournalValid(expected));
    assert(!std::memcmp(expected.operation, operation, 16) && proof.receipt.accepted == 5000000000ULL);
    request.generation = 2;
    assert(check() == ReceiptVerification::BindingMismatch && !proof.verified);
    request.generation = 1;
    author_hash[0] ^= 1;
    assert(check() == ReceiptVerification::BindingMismatch);
    author_hash[0] ^= 1;
    proof_hash[0] ^= 1;
    assert(check() == ReceiptVerification::BindingMismatch);
    proof_hash[0] ^= 1;
    delivery[0] ^= 1;
    assert(check() == ReceiptVerification::BindingMismatch);
    delivery[0] ^= 1;
    epoch[0] ^= 1;
    assert(check() == ReceiptVerification::BindingMismatch && !proof.verified);
    epoch[0] ^= 1;
    network_request[0] ^= 1;
    assert(check() == ReceiptVerification::BindingMismatch);
    network_request[0] ^= 1;
    // Rehashing corrupted signature bytes cannot establish directory authority.
    signed_bytes[receipt.size - 1] ^= 1;
    digest(receipt, proof_hash);
    assert(check() == ReceiptVerification::InvalidSignature && !proof.verified && !expected.sequence);
    signed_bytes[receipt.size - 1] ^= 1;
    digest(receipt, proof_hash);
    assert(check() == ReceiptVerification::Valid);
    assert(verifyIpConfirmation(request, receipt, manifest, 1, {author, 64}, {author_hash, 32}, crypto, hash,
                                signed_bytes, sizeof(signed_bytes), expected, proof) == ReceiptVerification::WorkspaceTooSmall);

    TrackConfirmationMemoryIo baseline;
    char head_a[160], head_b[160], reservation_path[160], signed_path[160], receipt_path[160], receipt_temp[160], upload_path[64], upload_a[64];
    assert(resolveObjectPath(request.object, ObjectFile::HeadA, {}, head_a, sizeof(head_a)) &&
           resolveObjectPath(request.object, ObjectFile::HeadB, {}, head_b, sizeof(head_b)) &&
           resolveObjectPath(request.object, ObjectFile::ReservationA, request.operation, reservation_path, sizeof(reservation_path)) &&
           resolveObjectPath(request.object, ObjectFile::SignedManifest, request.revision_hash, signed_path, sizeof(signed_path)) &&
           resolveObjectPath(request.object, ObjectFile::Receipt, request.network_request, receipt_path, sizeof(receipt_path)) &&
           resolveObjectPath(request.object, ObjectFile::ReceiptTemporary, request.network_request, receipt_temp, sizeof(receipt_temp)) &&
           resolveUploadPath(transfer, 16, UploadFile::Candidate, upload_path, sizeof(upload_path), UploadKind::IpReceipt) &&
           resolveUploadPath(transfer, 16, UploadFile::JournalA, upload_a, sizeof(upload_a), UploadKind::IpReceipt));
    RevisionReservation reservation;
    std::memcpy(reservation.operation, operation, 16);
    std::memcpy(reservation.object, manifest.object_id, 32);
    std::memcpy(reservation.author, author_hash, 32);
    std::memcpy(reservation.candidate, manifest.revision_hash, 32);
    reservation.generation = reservation.revision = reservation.sequence = 1;
    reservation.phase = ReservationPhase::Signed;
    ObjectHead initial;
    assert(createSignedObjectHead(manifest, reservation, 1, {author, 64}, {author_hash, 32}, nullptr, initial));
    assert(encodeObjectHead(initial, work_scratch, sizeof(work_scratch)));
    baseline.files[head_a] = {work_scratch, work_scratch + kObjectHeadBytes};
    assert(encodeReservation(reservation, work_scratch, sizeof(work_scratch)));
    baseline.files[reservation_path] = {work_scratch, work_scratch + kReservationBytes};
    UploadJournal upload;
    std::memcpy(upload.id, transfer, 16);
    std::memcpy(upload.author_hash, author_hash, 32);
    std::memcpy(upload.file_hash, proof_hash, 32);
    upload.generation = upload.sequence = 1;
    upload.total = upload.durable_offset = uint32_t(receipt.size);
    upload.phase = UploadPhase::Sealed;
    assert(encodeUploadJournal(upload, work_scratch, sizeof(work_scratch)));
    baseline.files[upload_a] = {work_scratch, work_scratch + kUploadJournalBytes};
    baseline.files[upload_path] = {receipt.data, receipt.data + receipt.size};
    baseline.files[signed_path] = {local_signed, local_signed + local_envelope.size()};
    using Work = IpConfirmationWork<TrackConfirmationMemoryIo, decltype(hash), decltype(manifest_crypto), decltype(crypto)>;
    static_assert(sizeof(Work) <= 4608, "Confirmation metadata must remain a bounded transient owner");
    for (unsigned mode = 0; mode < 7; ++mode)
    {
        auto io = baseline;
        if (mode == 1) io.files[upload_path][100] ^= 1;
        if (mode == 2) io.files[signed_path][100] ^= 1;
        if (mode == 5) io.fail_create = receipt_temp;
        if (mode == 6) io.fail_create = head_b;
        const auto before = io.files;
        auto work = std::make_unique<Work>(io, hash, manifest_crypto, crypto, request, geocaching::ByteView{author, 64},
                                           geocaching::ByteView{author_hash, 32}, work_signed, sizeof(work_signed), work_receipt, sizeof(work_receipt), work_domain, sizeof(work_domain));
        // Constructor freezes the network request before another task can alter input.
        if (mode == 0) network_request[0] ^= 1;
        auto result = IpConfirmationResult::Pending;
        for (unsigned step = 0; step < 32 && result == IpConfirmationResult::Pending; ++step)
        {
            result = work->step(mode == 3 ? 2 : 1, {author, 64}, {author_hash, 32}, mode != 4, work_scratch, sizeof(work_scratch));
            assert(!io.active);
        }
        if (mode == 0) network_request[0] ^= 1;
        if (mode == 0)
        {
            assert(result == IpConfirmationResult::Ready && work->result()->phase == HeadPhase::Committed && io.files.count(receipt_path));
            const auto durable = io.files;
            const auto writes = io.writes;
            work.reset();
            auto repeated = std::make_unique<Work>(io, hash, manifest_crypto, crypto, request, geocaching::ByteView{author, 64},
                                                   geocaching::ByteView{author_hash, 32}, work_signed, sizeof(work_signed), work_receipt, sizeof(work_receipt), work_domain, sizeof(work_domain));
            result = IpConfirmationResult::Pending;
            for (unsigned step = 0; step < 32 && result == IpConfirmationResult::Pending; ++step)
                result = repeated->step(1, {author, 64}, {author_hash, 32}, true, work_scratch, sizeof(work_scratch));
            assert(result == IpConfirmationResult::Ready && io.files == durable && io.writes == writes);
        }
        else if (mode < 5)
        {
            assert(result == (mode == 1 ? IpConfirmationResult::HashMismatch : mode == 2 ? IpConfirmationResult::Conflict
                                                                           : mode == 3   ? IpConfirmationResult::StaleIdentity
                                                                                         : IpConfirmationResult::StorageFailure));
            assert(io.files == before && io.writes == 0 && !work->result());
        }
        else
        {
            assert(result == IpConfirmationResult::StorageFailure && io.files[head_a] == before.at(head_a) && !io.files.count(head_b));
            if (mode == 6) assert(io.files.count(receipt_path));
            io.fail_create.clear();
            work.reset();
            auto resumed = std::make_unique<Work>(io, hash, manifest_crypto, crypto, request, geocaching::ByteView{author, 64},
                                                  geocaching::ByteView{author_hash, 32}, work_signed, sizeof(work_signed), work_receipt, sizeof(work_receipt), work_domain, sizeof(work_domain));
            result = IpConfirmationResult::Pending;
            for (unsigned step = 0; step < 32 && result == IpConfirmationResult::Pending; ++step)
                result = resumed->step(1, {author, 64}, {author_hash, 32}, true, work_scratch, sizeof(work_scratch));
            assert(result == IpConfirmationResult::Ready && resumed->result()->phase == HeadPhase::Committed);
        }
    }
}
