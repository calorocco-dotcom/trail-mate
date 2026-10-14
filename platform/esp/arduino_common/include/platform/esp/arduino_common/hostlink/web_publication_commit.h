#pragma once
#include "platform/esp/arduino_common/hostlink/web_publication_prepare.h"

namespace hostlink::management
{
// [prepare/operation_id16, committed_draft_hash32, author_generation32].
// The prepared ID is the durable operation ID; never generate another on retry.
inline Status cache_commit(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (capacity < 471) return Status::Internal;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    namespace sd = platform::esp::arduino_common::storage;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t expected_generation = 0;
    geocaching::ByteView operation, hash;
    if (!reader.array(fields, 3) || fields != 3 || !reader.binary(operation, 16) || operation.size != 16 ||
        !reader.binary(hash, 32) || hash.size != 32 || !reader.unsignedInteger(expected_generation) ||
        !expected_generation || expected_generation > UINT32_MAX || !reader.finished()) return Status::InvalidField;
    uint8_t retained[48], boot_id[16], bits = 0;
    std::memcpy(retained, operation.data, 16);
    std::memcpy(retained + 16, hash.data, 32);
    for (unsigned i = 0; i < 16; ++i) bits |= retained[i];
    if (!bits) return Status::InvalidField;
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    uint32_t actual = 0;
    const auto identity = adapter->readDeviceAuthorKey(output + 384, actual);
    if (identity == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
    if (identity != chat::DeviceAuthorResult::Ok) return Status::Busy;
    if (actual != expected_generation) return Status::StaleIdentity;
    uint64_t clock = 0;
    if (!runtime::publicationContext(boot_id, clock)) return Status::Busy;
    if (!loadPublicationTicket(output, capacity))
        return sd::sd_exists(kPublicationTicketPath) ? Status::StorageUnavailable : Status::Expired;
    const bool prepared = output[4] == static_cast<uint8_t>(PublicationTicketPhase::Prepared);
    const auto match = matchPublicationTicket(output, kPublicationTicketBytes, request.session, retained,
                                              prepared ? boot_id : output + 37, output + 384, actual, clock, prepared);
    if (match != Status::Ok) return match;
    if (std::memcmp(output + 177, retained + 16, 32)) return Status::Conflict;
    if (output[4] == static_cast<uint8_t>(PublicationTicketPhase::Checking)) return Status::Conflict;
    bool existing = false;
    if (prepared)
    {
        // A new ticket must not reuse an ID from any retained operation, even
        // if it is no longer the single current in-memory publication.
        std::memmove(output + 240, output, kPublicationTicketBytes);
        const auto* ticket = output + 240;
        auto prior = runtime::readWebPublicationJob(retained, ticket + 81, output, 240, written);
        if (prior == runtime::WebReadResult::NotFound)
            prior = runtime::readWebPublicationHistory(retained, ticket + 81, output, 240, written);
        if (prior == runtime::WebReadResult::Ready) return Status::Conflict;
        if (prior != runtime::WebReadResult::NotFound) return publication_result(prior);
        std::memmove(output, ticket, kPublicationTicketBytes);
    }
    if (!prepared)
    {
        // Preserve the private ticket while the queried task uses the reply arena.
        std::memmove(output + 240, output, kPublicationTicketBytes);
        auto* ticket = output + 240;
        auto result = runtime::readWebPublicationJob(retained, ticket + 81, output, 240, written);
        if (result == runtime::WebReadResult::NotFound)
            result = runtime::readWebPublicationHistory(retained, ticket + 81, output, 240, written);
        if (result == runtime::WebReadResult::Ready)
        {
            if (ticket[4] == static_cast<uint8_t>(PublicationTicketPhase::Intent) &&
                (!advancePublicationTicket(ticket, kPublicationTicketBytes, PublicationTicketPhase::Accepted) ||
                 !savePublicationTicket(ticket, kPublicationTicketBytes))) return Status::StorageUnavailable;
        }
        else
        {
            if (result != runtime::WebReadResult::NotFound) return publication_result(result);
            // Accepted-but-missing or old-boot evidence is unresolved. Do not
            // infer absence of signing/transport or create a replacement task.
            uint64_t created = 0;
            uint32_t lifetime = 0;
            for (unsigned i = 0; i < 8; ++i) created |= uint64_t(ticket[217 + i]) << (8 * i);
            for (unsigned i = 0; i < 4; ++i) lifetime |= uint32_t(ticket[225 + i]) << (8 * i);
            if (ticket[4] != static_cast<uint8_t>(PublicationTicketPhase::Intent) || std::memcmp(ticket + 37, boot_id, 16) ||
                clock < created || clock - created >= lifetime) return Status::Busy;
            std::memmove(output, ticket, kPublicationTicketBytes);
        }
        if (result == runtime::WebReadResult::Ready)
        {
            // The record is staged already; jump to the shared receipt encoding.
            existing = true;
        }
    }
    // A staged Accepted ticket means the existing operation supplied evidence.
    if (!existing)
    {
        uint64_t generation = 0;
        for (unsigned i = 0; i < 8; ++i) generation |= uint64_t(output[69 + i]) << (8 * i);
        const auto binding = runtime::readWebPublicationBinding(output + 53, generation, output + 145, output + 81, output + 240, 56);
        if (binding != runtime::WebReadResult::Ready) return publication_result(binding);
        if (std::memcmp(output + 177, output + 240, 32) || std::memcmp(output + 161, output + 272, 16) ||
            std::memcmp(output + 209, output + 288, 8)) return Status::Conflict;
        const auto refreshed_identity = adapter->readDeviceAuthorKey(output + 384, actual);
        if (refreshed_identity == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
        if (refreshed_identity != chat::DeviceAuthorResult::Ok) return Status::Busy;
        if (actual != expected_generation || std::memcmp(output + 81, output + 384, 64)) return Status::StaleIdentity;
        if (!runtime::publicationContext(boot_id, clock)) return Status::Busy;
        if (prepared)
        {
            const auto refreshed = matchPublicationTicket(output, kPublicationTicketBytes, request.session, retained, boot_id,
                                                          output + 384, actual, clock, true);
            if (refreshed != Status::Ok) return refreshed;
        }
        else
        {
            uint64_t created = 0;
            uint32_t lifetime = 0;
            for (unsigned i = 0; i < 8; ++i) created |= uint64_t(output[217 + i]) << (8 * i);
            for (unsigned i = 0; i < 4; ++i) lifetime |= uint32_t(output[225 + i]) << (8 * i);
            if (std::memcmp(output + 37, boot_id, 16) || clock < created || clock - created >= lifetime) return Status::Busy;
        }
        const uint32_t revision = uint32_t(output[213]) | uint32_t(output[214]) << 8 | uint32_t(output[215]) << 16 | uint32_t(output[216]) << 24;
        if (!advancePublicationTicket(output, kPublicationTicketBytes, PublicationTicketPhase::Intent) ||
            !savePublicationTicket(output, kPublicationTicketBytes)) return Status::StorageUnavailable;
        if (!runtime::queueWebDraftPublication(output + 53, generation, output + 145, output + 81, revision, output + 240, retained))
            return Status::Busy;
        if (!advancePublicationTicket(output, kPublicationTicketBytes, PublicationTicketPhase::Accepted) ||
            !savePublicationTicket(output, kPublicationTicketBytes)) return Status::StorageUnavailable;
        std::memmove(output + 240, output, kPublicationTicketBytes);
    }
    const auto* ticket = output + 240;
    uint64_t generation = 0;
    for (unsigned i = 0; i < 8; ++i) generation |= uint64_t(ticket[69 + i]) << (8 * i);
    const uint32_t revision = uint32_t(ticket[213]) | uint32_t(ticket[214]) << 8 | uint32_t(ticket[215]) << 16 | uint32_t(ticket[216]) << 24;
    geocaching::protocol::CmpWriter writer(output, 240);
    if (!writer.array(7) || !writer.binary({ticket + 21, 16}) || !writer.binary({ticket + 53, 16}) ||
        !writer.unsignedInteger(generation) || !writer.binary({ticket + 145, 16}) || !writer.binary({ticket + 177, 32}) ||
        !writer.unsignedInteger(revision) || !writer.unsignedInteger(1)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace hostlink::management
