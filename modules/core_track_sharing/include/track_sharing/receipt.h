#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "track_sharing/publication_journal.h"
namespace track_sharing
{
struct ReceiptView
{
    geocaching::ByteView raw, request, object, revision_hash, file_hash, directory_public, directory, epoch;
    uint64_t accepted = 0;
    uint32_t revision = 0, file_bytes = 0;
    uint8_t state = 0;
};
static_assert(sizeof(ReceiptView) <= 160, "Receipt must borrow payload fields");
// Wire structure only. No signature, directory provenance or job authority is
// established by this decoder. Retain immutable raw bytes until verification.
inline bool decodeReceipt(geocaching::ByteView raw, ReceiptView& output)
{
    if (signingSpansOverlap(raw.data, raw.size, reinterpret_cast<const uint8_t*>(&output), sizeof(output))) return false;
    output = {};
    if (!raw.data || !raw.size || raw.size > 1024) return false;
    geocaching::protocol::CmpReader reader(raw);
    size_t fields = 0;
    uint64_t value = 0;
    if (!reader.array(fields, 13) || fields != 13 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.binary(output.request, 16) || output.request.size != 16 || !reader.binary(output.object, 32) || output.object.size != 32 ||
        !reader.binary(output.revision_hash, 32) || output.revision_hash.size != 32 || !reader.unsignedInteger(value) || !value || value > UINT32_MAX) return false;
    output.revision = uint32_t(value);
    if (!reader.unsignedInteger(value) || value > 1) return false;
    output.state = uint8_t(value);
    if (!reader.binary(output.file_hash, 32) || output.file_hash.size != 32 || !reader.unsignedInteger(value) || !value || value > 8388608) return false;
    output.file_bytes = uint32_t(value);
    if (!reader.binary(output.directory_public, 64) || output.directory_public.size != 64 || !reader.binary(output.directory, 16) || output.directory.size != 16 ||
        !reader.binary(output.epoch, 16) || output.epoch.size != 16 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(output.accepted) || output.accepted > 253402300799ULL || !reader.finished()) return false;
    output.raw = raw;
    return true;
}
// Caller verifies expected directory public/delivery binding independently and
// checks signature before applying a Confirmed journal transition.
inline bool receiptMatchesJob(const ReceiptView& receipt, const PublicationJournal& job, geocaching::ByteView request,
                              geocaching::ByteView directory_public, uint8_t expected_state)
{
    return publicationJournalValid(job) && expected_state <= 1 && receipt.state == expected_state && request.data && request.size == 16 &&
           directory_public.data && directory_public.size == 64 && receipt.request.data && receipt.request.size == 16 &&
           receipt.object.data && receipt.object.size == 32 && receipt.revision_hash.data && receipt.revision_hash.size == 32 &&
           receipt.file_hash.data && receipt.file_hash.size == 32 && receipt.directory.data && receipt.directory.size == 16 &&
           receipt.epoch.data && receipt.epoch.size == 16 && receipt.directory_public.data && receipt.directory_public.size == 64 &&
           receipt.revision == job.revision && receipt.file_bytes == job.file_bytes &&
           !std::memcmp(receipt.request.data, request.data, 16) && !std::memcmp(receipt.object.data, job.object, 32) &&
           !std::memcmp(receipt.revision_hash.data, job.revision_hash, 32) && !std::memcmp(receipt.file_hash.data, job.file_hash, 32) &&
           !std::memcmp(receipt.directory.data, job.directory, 16) && !std::memcmp(receipt.epoch.data, job.directory_epoch, 16) &&
           !std::memcmp(receipt.directory_public.data, directory_public.data, 64);
}
} // namespace track_sharing
