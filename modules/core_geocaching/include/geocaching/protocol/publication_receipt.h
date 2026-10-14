#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/verify_record.h"

namespace geocaching::protocol
{
struct PublicationReceiptView
{
    ByteView directory, epoch, cache, hash;
    uint32_t revision = 0;
    uint8_t state = 0;
    uint64_t observed_at = 0;
};
// Inputs are immutable; the storage worker lends a disjoint 320-byte scratch
// area. This proof observes a head at a time, not a promise of future currency.
inline VerificationResult verifyPublicationReceipt(ByteView raw, ByteView signature, ByteView public_key,
                                                    ByteView directory, ByteView epoch, const VerifiedRecordView& content,
                                                    RecordCrypto& crypto, uint8_t* workspace, size_t capacity,
                                                    PublicationReceiptView& output)
{
    const auto overlaps = [](ByteView a, ByteView b)
    {
        if (!a.data || !b.data || !a.size || !b.size) return false;
        const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
        return x <= y ? y - x < a.size : x - y < b.size;
    };
    const ByteView result_bytes{reinterpret_cast<const uint8_t*>(&output), sizeof(output)};
    const ByteView inputs[] = {raw, signature, public_key, directory, epoch, content.record.encoded, content.signature,
                              {reinterpret_cast<const uint8_t*>(&content), sizeof(content)}};
    for (const auto input : inputs) if (overlaps(result_bytes, input)) return VerificationResult::InvalidRecord;
    if (overlaps(result_bytes, {workspace, capacity})) return VerificationResult::InvalidRecord;
    output = {};
    if (!raw.data || !raw.size || raw.size > 256 || !signature.data || signature.size != 64 ||
        !public_key.data || public_key.size != 64 || !directory.data || directory.size != 16 || !epoch.data || epoch.size != 16)
        return VerificationResult::InvalidRecord;
    if (!workspace || capacity < 320) return VerificationResult::WorkspaceTooSmall;
    for (const auto input : inputs)
    {
        const auto a = reinterpret_cast<uintptr_t>(workspace), b = reinterpret_cast<uintptr_t>(input.data);
        if (a <= b ? b - a < capacity : a - b < input.size) return VerificationResult::InvalidRecord;
    }
    CmpReader reader(raw);
    size_t count = 0;
    uint64_t version = 0, revision = 0, state = 0, observed = 0;
    ByteView destination, advertised_epoch, cache, hash;
    if (!reader.array(count, 8) || count != 8 || !reader.unsignedInteger(version) || version != 1 ||
        !reader.binary(destination, 16) || destination.size != 16 || !reader.binary(advertised_epoch, 16) || advertised_epoch.size != 16 ||
        !reader.binary(cache, 32) || cache.size != 32 || !reader.unsignedInteger(revision) || !revision || revision > UINT32_MAX ||
        !reader.binary(hash, 32) || hash.size != 32 || !reader.unsignedInteger(state) || state > 2 ||
        !reader.unsignedInteger(observed) || !observed || observed > 253402300799ULL || !reader.finished()) return VerificationResult::InvalidRecord;
    if (std::memcmp(destination.data, directory.data, 16) || std::memcmp(advertised_epoch.data, epoch.data, 16) ||
        std::memcmp(cache.data, content.id.bytes.data(), 32) || revision != content.record.revision ||
        std::memcmp(hash.data, content.hash.bytes.data(), 32) || state != static_cast<uint8_t>(content.record.state)) return VerificationResult::IdentityMismatch;
    CmpWriter canonical(workspace, capacity);
    if (!canonical.array(8) || !canonical.unsignedInteger(1) || !canonical.binary(destination) || !canonical.binary(advertised_epoch) ||
        !canonical.binary(cache) || !canonical.unsignedInteger(revision) || !canonical.binary(hash) || !canonical.unsignedInteger(state) ||
        !canonical.unsignedInteger(observed) || canonical.size() != raw.size || std::memcmp(workspace, raw.data, raw.size)) return VerificationResult::InvalidRecord;
    uint8_t digest[32];
    if (!crypto.sha256(public_key, workspace)) return VerificationResult::CryptoUnavailable;
    std::memmove(workspace + 10, workspace, 16);
    constexpr char name[] = "lxmf.delivery";
    if (!crypto.sha256({reinterpret_cast<const uint8_t*>(name), sizeof(name) - 1}, digest)) return VerificationResult::CryptoUnavailable;
    std::memcpy(workspace, digest, 10);
    if (!crypto.sha256({workspace, 26}, digest)) return VerificationResult::CryptoUnavailable;
    if (std::memcmp(digest, directory.data, 16)) return VerificationResult::IdentityMismatch;
    constexpr char domain[] = "trailmate.geocache/receipt/v1";
    std::memcpy(workspace, domain, sizeof(domain));
    std::memcpy(workspace + sizeof(domain), raw.data, raw.size);
    const auto result = crypto.verifyEd25519({public_key.data + 32, 32}, signature, {workspace, sizeof(domain) + raw.size});
    if (result != VerificationResult::Valid) return result;
    output = {destination, advertised_epoch, cache, hash, static_cast<uint32_t>(revision), static_cast<uint8_t>(state), observed};
    return VerificationResult::Valid;
}
} // namespace geocaching::protocol
