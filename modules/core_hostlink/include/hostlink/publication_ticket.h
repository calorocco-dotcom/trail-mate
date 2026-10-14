#pragma once

#include "hostlink/hostlink_codec.h"
#include "hostlink/management_envelope.h"
#include <cstring>

namespace hostlink::management
{
// Private SD record, never an arbitrary payload accepted for device signing.
// All arrays are borrowed; encoding uses the existing command arena.
struct PublicationTicketFields
{
    const uint8_t *owner, *token, *boot, *draft, *author, *directory, *directory_epoch, *content_hash;
    uint64_t draft_generation;
    uint32_t author_generation, from_revision, to_revision;
    uint64_t created_ms;
    uint32_t lifetime_ms;
};
constexpr size_t kPublicationTicketBytes = 231;
constexpr uint32_t kPublicationTicketLifetimeMs = 120000;
// Accepted describes local operation admission, never a directory receipt.
enum class PublicationTicketPhase : uint8_t
{
    Prepared = 0,
    Intent = 1,
    Accepted = 2,
    Checking = 3 // Network validation pending; cannot authorize COMMIT.
};

inline bool validatePublicationTicket(const uint8_t* bytes, size_t size)
{
    if (!bytes || size != kPublicationTicketBytes || std::memcmp(bytes, "TMP1", 4) || bytes[4] > 3) return false;
    const auto crc = crc16_ccitt(bytes, size - 2);
    if (bytes[size - 2] != static_cast<uint8_t>(crc) || bytes[size - 1] != static_cast<uint8_t>(crc >> 8)) return false;
    uint64_t generation = 0;
    uint32_t author_generation = 0, from = 0, to = 0, lifetime = 0;
    for (unsigned i = 0; i < 8; ++i) generation |= uint64_t(bytes[69 + i]) << (8 * i);
    for (unsigned i = 0; i < 4; ++i)
    {
        author_generation |= uint32_t(bytes[77 + i]) << (8 * i);
        from |= uint32_t(bytes[209 + i]) << (8 * i);
        to |= uint32_t(bytes[213 + i]) << (8 * i);
        lifetime |= uint32_t(bytes[225 + i]) << (8 * i);
    }
    return generation && author_generation && lifetime && lifetime <= kPublicationTicketLifetimeMs && to &&
           (to == from || (from != UINT32_MAX && to == from + 1));
}

inline bool encodePublicationTicket(const PublicationTicketFields& fields, uint8_t* output, size_t capacity,
                                    PublicationTicketPhase phase = PublicationTicketPhase::Prepared)
{
    if (!output || capacity < kPublicationTicketBytes || !fields.owner || !fields.token || !fields.boot ||
        !fields.draft || !fields.author || !fields.directory || !fields.directory_epoch || !fields.content_hash ||
        (phase != PublicationTicketPhase::Prepared && phase != PublicationTicketPhase::Checking)) return false;
    // Inputs must not overlap output: callers encode only after request views have been consumed.
    std::memcpy(output, "TMP1", 4);
    output[4] = static_cast<uint8_t>(phase);
    std::memcpy(output + 5, fields.owner, 16);
    std::memcpy(output + 21, fields.token, 16);
    std::memcpy(output + 37, fields.boot, 16);
    std::memcpy(output + 53, fields.draft, 16);
    for (unsigned i = 0; i < 8; ++i)
    {
        output[69 + i] = static_cast<uint8_t>(fields.draft_generation >> (8 * i));
        output[217 + i] = static_cast<uint8_t>(fields.created_ms >> (8 * i));
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        output[77 + i] = static_cast<uint8_t>(fields.author_generation >> (8 * i));
        output[209 + i] = static_cast<uint8_t>(fields.from_revision >> (8 * i));
        output[213 + i] = static_cast<uint8_t>(fields.to_revision >> (8 * i));
        output[225 + i] = static_cast<uint8_t>(fields.lifetime_ms >> (8 * i));
    }
    std::memcpy(output + 81, fields.author, 64);
    std::memcpy(output + 145, fields.directory, 16);
    std::memcpy(output + 161, fields.directory_epoch, 16);
    std::memcpy(output + 177, fields.content_hash, 32);
    const auto crc = crc16_ccitt(output, kPublicationTicketBytes - 2);
    output[kPublicationTicketBytes - 2] = static_cast<uint8_t>(crc);
    output[kPublicationTicketBytes - 1] = static_cast<uint8_t>(crc >> 8);
    return validatePublicationTicket(output, kPublicationTicketBytes);
}

inline Status matchPublicationTicket(const uint8_t* bytes, size_t size, const uint8_t owner[16],
                                     const uint8_t token[16], const uint8_t boot[16], const uint8_t author[64],
                                     uint32_t author_generation, uint64_t now_ms, bool for_commit)
{
    if (!validatePublicationTicket(bytes, size) || !owner || !token || !boot || !author) return Status::InvalidField;
    if (std::memcmp(bytes + 5, owner, 16) || std::memcmp(bytes + 21, token, 16)) return Status::Unauthorized;
    uint32_t generation = 0, lifetime = 0;
    uint64_t created = 0;
    for (unsigned i = 0; i < 8; ++i) created |= uint64_t(bytes[217 + i]) << (8 * i);
    for (unsigned i = 0; i < 4; ++i)
    {
        generation |= uint32_t(bytes[77 + i]) << (8 * i);
        lifetime |= uint32_t(bytes[225 + i]) << (8 * i);
    }
    if (std::memcmp(bytes + 81, author, 64) || generation != author_generation) return Status::StaleIdentity;
    if (std::memcmp(bytes + 37, boot, 16)) return Status::Conflict;
    // Intent/accepted records are queryable after expiry, but cannot authorize a new commit.
    if (for_commit && bytes[4] != static_cast<uint8_t>(PublicationTicketPhase::Prepared)) return Status::Conflict;
    if ((for_commit || bytes[4] == 0 || bytes[4] == 3) && (now_ms < created || now_ms - created >= lifetime)) return Status::Expired;
    return Status::Ok;
}

inline bool advancePublicationTicket(uint8_t* bytes, size_t size, PublicationTicketPhase next)
{
    if (!validatePublicationTicket(bytes, size) || bytes[4] == 3) return false;
    const auto phase = static_cast<uint8_t>(next);
    if (phase > 2 || phase < bytes[4] || phase > bytes[4] + 1) return false;
    bytes[4] = phase;
    const auto crc = crc16_ccitt(bytes, size - 2);
    bytes[size - 2] = static_cast<uint8_t>(crc);
    bytes[size - 1] = static_cast<uint8_t>(crc >> 8);
    return true;
}
inline bool completePublicationCheck(uint8_t* bytes, size_t size, uint64_t now_ms)
{
    if (!validatePublicationTicket(bytes, size) || bytes[4] != 3) return false;
    bytes[4] = 0;
    for (unsigned i = 0; i < 8; ++i) bytes[217 + i] = static_cast<uint8_t>(now_ms >> (8 * i));
    const auto crc = crc16_ccitt(bytes, size - 2);
    bytes[size - 2] = static_cast<uint8_t>(crc);
    bytes[size - 1] = static_cast<uint8_t>(crc >> 8);
    return true;
}
} // namespace hostlink::management
