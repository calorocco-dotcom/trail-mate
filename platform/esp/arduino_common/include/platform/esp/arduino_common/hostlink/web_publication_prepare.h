#pragma once
#include "app/app_facade_access.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/hostlink/publication_ticket_store.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"

namespace hostlink::management
{
inline bool terminal_publication_result(geocaching::ByteView reply, const uint8_t operation[16])
{
    geocaching::protocol::CmpReader reader(reply);
    size_t fields = 0;
    uint64_t phase = 0, revision = 0, durable = 0, transported = 0;
    geocaching::ByteView id, cache, hash;
    if (!reader.array(fields, 8) || fields != 8 || !reader.binary(id, 16) || id.size != 16 || std::memcmp(id.data, operation, 16) ||
        !reader.unsignedInteger(phase) || (phase != 3 && phase != 4) || !reader.binary(cache, 32) || cache.size != 32 ||
        !reader.binary(hash, 32) || hash.size != 32 || !reader.unsignedInteger(revision) || !revision || revision > UINT32_MAX ||
        !reader.unsignedInteger(durable) || durable != 1) return false;
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.unsignedInteger(transported) || transported != 1) return false;
    return (phase != 3 || transported == 1) && reader.nil() && reader.finished();
}
inline Status publication_result(platform::esp::arduino_common::geocaching::browse_runtime::WebReadResult result)
{
    using Result = platform::esp::arduino_common::geocaching::browse_runtime::WebReadResult;
    switch (result)
    {
    case Result::Ready:
        return Status::Ok;
    case Result::Pending:
        return Status::Busy;
    case Result::Invalid:
        return Status::InvalidField;
    case Result::Forbidden:
        return Status::Unauthorized;
    case Result::StaleIdentity:
        return Status::StaleIdentity;
    case Result::Unavailable:
        return Status::StorageUnavailable;
    case Result::HashMismatch:
        return Status::HashMismatch;
    default:
        return Status::Conflict;
    }
}
inline bool derive_prepare_cache_id(const uint8_t author[64], const uint8_t draft[16], uint8_t output[32])
{
    constexpr char domain[] = "trailmate.geocache/id/v1";
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    namespace crypto = platform::esp::common::crypto;
    const bool ok = crypto::sha256_starts(&context, 0) == 0 &&
                    crypto::sha256_update(&context, reinterpret_cast<const uint8_t*>(domain), sizeof(domain)) == 0 &&
                    crypto::sha256_update(&context, author, 64) == 0 && crypto::sha256_update(&context, draft, 16) == 0 &&
                    crypto::sha256_finish(&context, output) == 0;
    mbedtls_sha256_free(&context);
    return ok;
}
// [draft16, generation64, directory16, author_generation32, prepare_id16].
// Checking is persisted before network polling; COMMIT must reject it.
inline Status cache_prepare(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (capacity < 471) return Status::Internal;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    namespace sd = platform::esp::arduino_common::storage;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    geocaching::ByteView draft, directory, token;
    uint64_t generation = 0, expected_author_generation = 0;
    if (!reader.array(fields, 5) || fields != 5 || !reader.binary(draft, 16) || draft.size != 16 ||
        !reader.unsignedInteger(generation) || !generation || !reader.binary(directory, 16) || directory.size != 16 ||
        !reader.unsignedInteger(expected_author_generation) || !expected_author_generation || expected_author_generation > UINT32_MAX ||
        !reader.binary(token, 16) || token.size != 16 || !reader.finished()) return Status::InvalidField;
    uint8_t ids[48], boot_id[16];
    std::memcpy(ids, draft.data, 16);
    std::memcpy(ids + 16, directory.data, 16);
    std::memcpy(ids + 32, token.data, 16);
    uint8_t token_bits = 0;
    for (unsigned i = 32; i < 48; ++i) token_bits |= ids[i];
    if (!token_bits) return Status::InvalidField;
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    uint32_t actual = 0;
    const auto author_result = adapter->readDeviceAuthorKey(output + 384, actual);
    if (author_result == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
    if (author_result != chat::DeviceAuthorResult::Ok) return Status::Busy;
    if (actual != expected_author_generation) return Status::StaleIdentity;
    uint64_t clock = 0;
    if (!runtime::publicationContext(boot_id, clock)) return Status::Busy;
    bool exists = loadPublicationTicket(output, capacity);
    if (!exists && sd::sd_exists(kPublicationTicketPath)) return Status::StorageUnavailable;
    if (exists)
    {
        uint64_t created = 0;
        uint32_t lifetime = 0;
        for (unsigned i = 0; i < 8; ++i) created |= uint64_t(output[217 + i]) << (8 * i);
        for (unsigned i = 0; i < 4; ++i) lifetime |= uint32_t(output[225 + i]) << (8 * i);
        const bool same_token = !std::memcmp(output + 21, ids + 32, 16);
        if (output[4] == 1) return Status::Conflict;
        if (output[4] == 2)
        {
            if (same_token) return Status::Conflict;
            if (std::memcmp(output + 5, request.session, 16)) return Status::Unauthorized;
            if (std::memcmp(output + 81, output + 384, 64)) return Status::StaleIdentity;
            uint32_t prior_generation = 0;
            for (unsigned i = 0; i < 4; ++i) prior_generation |= uint32_t(output[77 + i]) << (8 * i);
            if (prior_generation != actual) return Status::StaleIdentity;
            std::memmove(output + 240, output, kPublicationTicketBytes);
            auto* prior = output + 240;
            size_t result_size = 0;
            auto result = runtime::readWebPublicationJob(prior + 21, prior + 81, output, 240, result_size);
            if (result == runtime::WebReadResult::NotFound)
                result = runtime::readWebPublicationHistory(prior + 21, prior + 81, output, 240, result_size);
            if (result != runtime::WebReadResult::Ready) return publication_result(result);
            if (!terminal_publication_result({output, result_size}, prior + 21)) return Status::Conflict;
            std::memmove(output, prior, kPublicationTicketBytes);
            std::memcpy(output + 384, output + 81, 64);
            exists = false;
        }
        if (exists)
        {
            const bool old_boot = std::memcmp(output + 37, boot_id, 16) != 0;
            const bool expired = clock < created || clock - created >= lifetime;
            uint32_t prior_generation = 0;
            for (unsigned i = 0; i < 4; ++i) prior_generation |= uint32_t(output[77 + i]) << (8 * i);
            const bool same_owner = !std::memcmp(output + 5, request.session, 16) && !std::memcmp(output + 81, output + 384, 64) && prior_generation == actual;
            if (!same_token && (old_boot || expired || same_owner)) exists = false;
            else
            {
                const auto match = matchPublicationTicket(output, kPublicationTicketBytes, request.session, ids + 32,
                                                          boot_id, output + 384, actual, clock, false);
                if (match != Status::Ok) return match;
                if (std::memcmp(output + 53, ids, 16) || std::memcmp(output + 145, ids + 16, 16)) return Status::Conflict;
                for (unsigned i = 0; i < 8; ++i)
                    if (output[69 + i] != static_cast<uint8_t>(generation >> (8 * i))) return Status::Conflict;
            }
        }
    }
    if (!exists || output[4] == 3)
    {
        const auto binding = runtime::readWebPublicationBinding(ids, generation, ids + 16, output + 384, output + 240, 88);
        if (binding != runtime::WebReadResult::Ready) return publication_result(binding);
        if (!exists)
        {
            uint32_t from = 0, to = 0;
            for (unsigned i = 0; i < 4; ++i)
            {
                from |= uint32_t(output[288 + i]) << (8 * i);
                to |= uint32_t(output[292 + i]) << (8 * i);
            }
            PublicationTicketFields ticket{request.session, ids + 32, boot_id, ids, output + 384, ids + 16,
                                           output + 272, output + 240, generation, actual, from, to, clock, kPublicationTicketLifetimeMs};
            if (!encodePublicationTicket(ticket, output, capacity, PublicationTicketPhase::Checking) ||
                !savePublicationTicket(output, kPublicationTicketBytes)) return Status::StorageUnavailable;
        }
        else if (std::memcmp(output + 177, output + 240, 32) || std::memcmp(output + 161, output + 272, 16) ||
                 std::memcmp(output + 209, output + 288, 8)) return Status::Conflict;
        if (!derive_prepare_cache_id(output + 384, ids, output + 328)) return Status::Internal;
        const auto latest = runtime::readWebLatestRevision(output + 328, ids + 16, output + 240, 36);
        const uint32_t from = uint32_t(output[209]) | uint32_t(output[210]) << 8 | uint32_t(output[211]) << 16 | uint32_t(output[212]) << 24;
        if (latest == runtime::WebReadResult::Absent)
        {
            if (from) return Status::Conflict;
        }
        else if (latest == runtime::WebReadResult::Ready)
        {
            const uint32_t revision = uint32_t(output[240]) | uint32_t(output[241]) << 8 | uint32_t(output[242]) << 16 | uint32_t(output[243]) << 24;
            if (!from || revision != from || std::memcmp(output + 244, output + 296, 32)) return Status::Conflict;
        }
        else return publication_result(latest);
        if (!runtime::publicationContext(boot_id, clock) || std::memcmp(output + 37, boot_id, 16)) return Status::Conflict;
        const auto current = matchPublicationTicket(output, kPublicationTicketBytes, request.session, ids + 32,
                                                    boot_id, output + 384, actual, clock, false);
        if (current != Status::Ok) return current;
        if (!completePublicationCheck(output, kPublicationTicketBytes, clock) || !savePublicationTicket(output, kPublicationTicketBytes)) return Status::StorageUnavailable;
    }
    uint64_t created = 0;
    for (unsigned i = 0; i < 8; ++i) created |= uint64_t(output[217 + i]) << (8 * i);
    const auto lifetime = uint32_t(output[225]) | uint32_t(output[226]) << 8 | uint32_t(output[227]) << 16 | uint32_t(output[228]) << 24;
    const auto remaining = lifetime - static_cast<uint32_t>(clock - created);
    // Stage immutable ticket above the response area, avoiding overlapping CMP copies.
    std::memmove(output + 240, output, kPublicationTicketBytes);
    const auto* ticket = output + 240;
    const uint32_t from = uint32_t(ticket[209]) | uint32_t(ticket[210]) << 8 | uint32_t(ticket[211]) << 16 | uint32_t(ticket[212]) << 24;
    const uint32_t to = uint32_t(ticket[213]) | uint32_t(ticket[214]) << 8 | uint32_t(ticket[215]) << 16 | uint32_t(ticket[216]) << 24;
    geocaching::protocol::CmpWriter writer(output, 240);
    if (!writer.array(10) || !writer.binary({ticket + 21, 16}) || !writer.binary({ticket + 53, 16}) ||
        !writer.unsignedInteger(generation) || !writer.binary({ticket + 81, 64}) || !writer.binary({ticket + 145, 16}) ||
        !writer.binary({ticket + 161, 16}) || !writer.binary({ticket + 177, 32}) || !writer.unsignedInteger(from) ||
        !writer.unsignedInteger(to) || !writer.unsignedInteger(remaining)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace hostlink::management
