#pragma once
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/publish_response.h"
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/publication_recovery.h"

namespace geocaching::storage
{
enum class PublicationHistoryJobResult : uint8_t
{
    Ready,
    Invalid,
    Forbidden,
    Unavailable,
    WorkspaceTooSmall
};

// Read-only projection. The store's root/read lease must still own evidence.
// Verification result and workspace are caller-owned to avoid another large
// protocol object on ESP stacks. No submit, resume, or transport side effect.
inline PublicationHistoryJobResult encodePublicationHistoryJob(const PublicationRecoveryView& evidence,
                                                               ByteView operation, ByteView author,
                                                               protocol::RecordCrypto& crypto,
                                                               uint8_t* workspace, size_t workspace_capacity,
                                                               protocol::VerifiedRecordView& verified,
                                                               uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!operation.data || operation.size != 16 || !author.data || author.size != 64 || !output || capacity < 192 ||
        std::memcmp(operation.data, evidence.task.data(), 16) || std::memcmp(operation.data, evidence.key.data() + 32, 16) ||
        evidence.outgoing_state > 5 || evidence.task_state > 5 || evidence.confirmed != (evidence.outgoing_state == 4) ||
        (evidence.confirmed && (evidence.task_state != 3 || !evidence.response.size)) ||
        (!evidence.confirmed && evidence.response.size)) return PublicationHistoryJobResult::Invalid;
    uint8_t bits = 0;
    for (size_t i = 0; i < operation.size; ++i) bits |= operation.data[i];
    if (!bits) return PublicationHistoryJobResult::Invalid;
    const auto overlaps = [](ByteView input, const uint8_t* target, size_t size)
    {
        if (!input.size) return false;
        if (!input.data || !target) return true;
        const auto source = reinterpret_cast<uintptr_t>(input.data), destination = reinterpret_cast<uintptr_t>(target);
        return source <= destination ? destination - source < input.size : source - destination < size;
    };
    if (overlaps(evidence.request, output, capacity) || overlaps(evidence.response, output, capacity) ||
        overlaps(evidence.request, workspace, workspace_capacity) || overlaps(evidence.response, workspace, workspace_capacity))
        return PublicationHistoryJobResult::Invalid;
    RequestId request;
    std::memcpy(request.bytes.data(), operation.data, 16);
    protocol::PublishRequestView published;
    if (!protocol::decodePublishRequest(evidence.request, request, published)) return PublicationHistoryJobResult::Invalid;
    const auto result = protocol::verifyGeocache(published.signed_cache, crypto, workspace, workspace_capacity,
                                                 verified, &evidence.cache, &evidence.hash);
    if (result == protocol::VerificationResult::CryptoUnavailable) return PublicationHistoryJobResult::Unavailable;
    if (result == protocol::VerificationResult::WorkspaceTooSmall) return PublicationHistoryJobResult::WorkspaceTooSmall;
    if (result != protocol::VerificationResult::Valid) return PublicationHistoryJobResult::Invalid;
    if (std::memcmp(verified.record.author_public_key.data, author.data, 64)) return PublicationHistoryJobResult::Forbidden;
    if (evidence.confirmed)
    {
        protocol::PublishDisposition disposition;
        if (!protocol::decodePublishResponse(evidence.response, request, verified.id, verified.hash,
                                             verified.record.revision, verified.record.state, disposition))
            return PublicationHistoryJobResult::Invalid;
    }
    // A durable stop or revoked continuation doesn't establish whether a
    // previously admitted request reached the directory. Preserve unknown.
    const char* error = evidence.outgoing_state == 5 ? "Local publication storage failed"
                        : evidence.task_state == 4   ? "Publication task is blocked"
                                                     : nullptr;
    const uint8_t phase = evidence.confirmed ? 3 : error                                               ? 5
                                               : evidence.task_state == 5 || !evidence.continue_intent ? 4
                                                                                                       : 2;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(8) || !writer.binary({request.bytes.data(), 16}) || !writer.unsignedInteger(phase) ||
        !writer.binary({verified.id.bytes.data(), 32}) || !writer.binary({verified.hash.bytes.data(), 32}) ||
        !writer.unsignedInteger(verified.record.revision) || !writer.unsignedInteger(1) ||
        !(evidence.confirmed ? writer.unsignedInteger(1) : writer.nil()) || !(error ? writer.text(error) : writer.nil()))
        return PublicationHistoryJobResult::WorkspaceTooSmall;
    written = writer.size();
    return PublicationHistoryJobResult::Ready;
}
} // namespace geocaching::storage
