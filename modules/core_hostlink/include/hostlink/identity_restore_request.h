#pragma once
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "geocaching/protocol/cmp_reader.h"
#include "hostlink/management_envelope.h"
#include <cstring>

namespace hostlink::management
{
struct IdentityRestoreView
{
    uint32_t expected_generation = 0, backup_generation = 0;
    uint8_t format = 0;
    geocaching::ByteView expected_public{}, payload{}, signature{}, public_key{}, private_key{}, identity_hash{}, delivery{};
};
// Request: [targetGeneration,targetPublic64,payloadBin,signature64,consentBool].
// Views borrow the USB command buffer. Caller must not overwrite it until the
// authorized atomic write completes, and must wipe the command on every exit.
inline Status parseIdentityRestoreRequest(geocaching::ByteView input, IdentityRestoreView& output)
{
    output = {};
    if (!input.data || input.size > 448) return Status::InvalidField;
    geocaching::protocol::CmpReader reader(input);
    IdentityRestoreView parsed;
    size_t count = 0;
    uint64_t generation = 0;
    if (!reader.array(count, 5) || count != 5 || !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.binary(parsed.expected_public, 64) || parsed.expected_public.size != 64 ||
        !reader.binary(parsed.payload, 320) || !reader.binary(parsed.signature, 64) || parsed.signature.size != 64 ||
        reader.position() + 1 != input.size) return Status::InvalidField;
    if (input.data[reader.position()] == 0xc2) return Status::Unauthorized;
    if (input.data[reader.position()] != 0xc3) return Status::InvalidField;
    parsed.expected_generation = uint32_t(generation);
    geocaching::protocol::CmpReader payload(parsed.payload);
    uint64_t version = 0, format = 0;
    std::string_view type;
    if (!payload.array(count, 8) || count != 8 || !payload.unsignedInteger(version) || version != 1 ||
        !payload.text(type, 32) || type != "trailmate.identity.backup.v1" || !payload.unsignedInteger(format)) return Status::InvalidField;
    if (format > 1) return Status::Unsupported;
    parsed.format = uint8_t(format);
    if (!payload.binary(parsed.public_key, 64) || parsed.public_key.size != 64 ||
        !payload.binary(parsed.private_key, 96) || parsed.private_key.size != (format == 0 ? 96U : 64U) ||
        !payload.binary(parsed.identity_hash, 16) || parsed.identity_hash.size != 16 ||
        !payload.binary(parsed.delivery, 16) || parsed.delivery.size != 16 ||
        !payload.unsignedInteger(generation) || !generation || generation > UINT32_MAX || !payload.finished()) return Status::InvalidField;
    parsed.backup_generation = uint32_t(generation);
    output = parsed;
    return Status::Ok;
}
// This proves the backup author's signature. The caller still checks both
// private/public pairs and derived addresses, and binds target generation/public
// under the identity lock before admitting storage or invalidating sessions.
inline bool verifyIdentityRestoreSignature(const IdentityRestoreView& value, uint8_t* work, size_t capacity)
{
    static constexpr char domain[] = "TrailMate.Identity.Backup.v1";
    if (!work || !value.payload.data || !value.public_key.data || !value.signature.data ||
        value.public_key.size != 64 || value.signature.size != 64 || value.payload.size > 320 ||
        capacity < sizeof(domain) + value.payload.size) return false;
    const auto start = reinterpret_cast<uintptr_t>(work), input = reinterpret_cast<uintptr_t>(value.payload.data);
    const size_t used = sizeof(domain) + value.payload.size;
    if (start >= input ? start - input < value.payload.size : input - start < used) return false;
    const auto signature = reinterpret_cast<uintptr_t>(value.signature.data);
    const auto public_key = reinterpret_cast<uintptr_t>(value.public_key.data);
    if ((start >= signature ? start - signature < 64 : signature - start < used) ||
        (start >= public_key ? start - public_key < 64 : public_key - start < used)) return false;
    std::memcpy(work, domain, sizeof(domain));
    std::memcpy(work + sizeof(domain), value.payload.data, value.payload.size);
    const bool valid = ed25519_verify(value.signature.data, work, used, value.public_key.data + 32) != 0;
    volatile uint8_t* clear = work;
    for (size_t i = 0; i < used; ++i) clear[i] = 0;
    return valid;
}
} // namespace hostlink::management
