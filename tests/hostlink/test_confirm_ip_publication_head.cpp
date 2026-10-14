#include "track_sharing/confirm_ip_publication_head.h"
#include <array>
#include <cassert>
using namespace track_sharing;
struct Heads
{
    ObjectHead stored;
    bool fail = false;
    unsigned writes = 0;
    HeadStoreResult load(ObjectHead& current, ObjectHead&, uint8_t*, size_t)
    {
        current = stored;
        return HeadStoreResult::Ready;
    }
    HeadStoreResult advance(const ObjectHead& current, const ObjectHead& next, ObjectHead&, uint8_t*, size_t)
    {
        assert(objectHeadAdvanceValid(current, next));
        if (fail) return HeadStoreResult::StorageFailure;
        stored = next;
        ++writes;
        return HeadStoreResult::Ready;
    }
};
struct Receipts
{
    bool durable = false;
    SignedStoreResult result = SignedStoreResult::Ready;
    unsigned calls = 0;
    SignedStoreResult persist(geocaching::ByteView, geocaching::ByteView, uint8_t*, size_t)
    {
        ++calls;
        if (result == SignedStoreResult::Ready) durable = true;
        return result;
    }
};
int main()
{
    std::array<uint8_t, 64> author{}, directory{}, signature{};
    std::array<uint8_t, 32> author_hash{}, file_hash{};
    std::array<uint8_t, 16> request{};
    std::array<uint8_t, 128> scratch{};
    author.fill(1);
    directory.fill(2);
    signature.fill(3);
    author_hash.fill(4);
    file_hash.fill(5);
    request.fill(6);
    RevisionReservation reservation;
    std::memset(reservation.operation, 7, 16);
    std::memset(reservation.object, 8, 32);
    std::memcpy(reservation.author, author_hash.data(), 32);
    std::memset(reservation.candidate, 9, 32);
    reservation.sequence = 1;
    reservation.generation = 1;
    reservation.revision = 1;
    reservation.phase = ReservationPhase::Signed;
    VerifiedManifestView manifest;
    manifest.verified = true;
    manifest.manifest.author = {author.data(), 64};
    manifest.manifest.revision = 1;
    manifest.manifest.file_bytes = 100;
    manifest.manifest.file_hash = {file_hash.data(), 32};
    std::memcpy(manifest.object_id, reservation.object, 32);
    std::memcpy(manifest.revision_hash, reservation.candidate, 32);
    Heads heads;
    assert(createSignedObjectHead(manifest, reservation, 1, {author.data(), 64}, {author_hash.data(), 32}, nullptr, heads.stored));
    PublicationJournal expected;
    std::memcpy(expected.operation, reservation.operation, 16);
    std::memcpy(expected.object, reservation.object, 32);
    std::memcpy(expected.revision_hash, reservation.candidate, 32);
    std::memcpy(expected.author_hash, author_hash.data(), 32);
    std::memcpy(expected.file_hash, file_hash.data(), 32);
    std::memset(expected.directory, 10, 16);
    std::memset(expected.directory_epoch, 11, 16);
    expected.sequence = 1;
    expected.generation = 1;
    expected.revision = 1;
    expected.file_bytes = expected.offset = 100;
    expected.phase = PublicationPhase::Publishing;
    VerifiedReceiptView proof;
    proof.verified = true;
    proof.signature = {signature.data(), 64};
    auto& receipt = proof.receipt;
    receipt.raw = {signature.data(), 1};
    receipt.request = {request.data(), 16};
    receipt.object = {expected.object, 32};
    receipt.revision_hash = {expected.revision_hash, 32};
    receipt.file_hash = {file_hash.data(), 32};
    receipt.directory_public = {directory.data(), 64};
    receipt.directory = {expected.directory, 16};
    receipt.epoch = {expected.directory_epoch, 16};
    receipt.revision = 1;
    receipt.file_bytes = 100;
    ObjectHead current, next, verify;
    Receipts receipts;
    const auto confirm = [&]
    { return confirmIpPublicationHead(heads, receipts, proof, expected, {request.data(), 16}, {directory.data(), 64},
                                      manifest, reservation, 1, {author.data(), 64}, {author_hash.data(), 32}, current, next, verify, scratch.data(), scratch.size()); };
    // No persistence is permitted for unverified, wrong-file or wrong-identity evidence.
    proof.verified = false;
    assert(confirm() == HeadStoreResult::Invalid && receipts.calls == 0);
    proof.verified = true;
    receipt.file_bytes = 99;
    assert(confirm() == HeadStoreResult::Invalid && receipts.calls == 0);
    receipt.file_bytes = 100;
    heads.stored.generation = 2;
    assert(confirm() == HeadStoreResult::Conflict && receipts.calls == 0);
    heads.stored.generation = 1;
    expected.operation[0] ^= 1;
    assert(confirm() == HeadStoreResult::Conflict && receipts.calls == 0);
    expected.operation[0] ^= 1;
    // Receipt storage failure leaves Signed intact.
    receipts.result = SignedStoreResult::StorageFailure;
    assert(confirm() == HeadStoreResult::StorageFailure && heads.writes == 0 && heads.stored.phase == HeadPhase::Signed);
    receipts.result = SignedStoreResult::Ready;
    heads.fail = true;
    assert(confirm() == HeadStoreResult::StorageFailure && receipts.durable && heads.writes == 0);
    // Retrying after receipt persistence/failed head write performs one transition.
    heads.fail = false;
    assert(confirm() == HeadStoreResult::Ready && heads.writes == 1 && heads.stored.phase == HeadPhase::Committed);
    assert(confirm() == HeadStoreResult::Ready && heads.writes == 1);
    // Committed metadata alone is insufficient when exact receipt persistence conflicts.
    receipts.result = SignedStoreResult::Conflict;
    assert(confirm() == HeadStoreResult::Conflict && heads.writes == 1);
}
