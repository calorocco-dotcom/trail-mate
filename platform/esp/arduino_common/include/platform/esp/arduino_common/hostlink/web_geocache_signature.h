#pragma once
#include "hostlink/geocache_sign_request.h"
#include "platform/esp/arduino_common/hostlink/web_config_upload.h"
#include "platform/esp/arduino_common/hostlink/web_publication_prepare.h"

namespace hostlink::management
{
inline Status cache_signature(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || capacity < 448) return Status::Internal;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    uint32_t offset = 0;
    uint16_t maximum = 0;
    if (request.code == static_cast<uint16_t>(Operation::CacheSignBegin))
    {
        GeocacheSignRequest decoded;
        const auto parsed = parseGeocacheSignRequest(request.body, request.body_size, decoded);
        if (parsed != Status::Ok) return parsed;
        uint8_t draft[16];
        const bool bound = decoded.draft.size == 16;
        const auto draft_generation = decoded.draft_generation;
        if (bound) std::memcpy(draft, decoded.draft.data, 16);
        // Copy borrowed bindings before metadata/hash scratch overwrites input.
        std::memmove(output + 320, decoded.transfer.data, 16);
        std::memmove(output + 336, decoded.hash.data, 32);
        std::memmove(output + 368, decoded.operation.data, 16);
        const auto total = decoded.bytes, expected_generation = decoded.author_generation;
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        uint32_t actual = 0;
        if (!adapter) return Status::Busy;
        const auto identity = adapter->readDeviceAuthorKey(output + 384, actual);
        if (identity != chat::DeviceAuthorResult::Ok) return identity == chat::DeviceAuthorResult::StorageError ? Status::StorageUnavailable : Status::Busy;
        if (actual != expected_generation) return Status::StaleIdentity;
        web_config::Metadata metadata;
        if (!web_config::load(metadata, output)) return Status::Expired;
        if (std::memcmp(metadata.owner, request.session, 16)) return Status::Unauthorized;
        if (metadata.kind != 5 || metadata.phase != 1 || metadata.total != total ||
            std::memcmp(metadata.id, output + 320, 16) || std::memcmp(metadata.hash, output + 336, 32)) return Status::Conflict;
        const auto queued = runtime::queueWebSignature(output + 368, request.session, output + 320, output + 336, total, output + 384, actual,
                                                       bound ? draft : nullptr, draft_generation);
        if (queued != runtime::WebReadResult::Ready) return publication_result(queued);
        return publication_result(runtime::readWebSignature(output + 368, request.session, true, 0, 0, output, 320, written));
    }
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t position = 0, limit = 0;
    geocaching::ByteView operation;
    const bool read = request.code == static_cast<uint16_t>(Operation::CacheSignRead);
    if (!reader.array(fields, read ? 3 : 1) || fields != (read ? 3 : 1) || !reader.binary(operation, 16) || operation.size != 16 ||
        (read && (!reader.unsignedInteger(position) || position > 4165 || !reader.unsignedInteger(limit) || !limit || limit > 320)) || !reader.finished()) return Status::InvalidField;
    std::memmove(output + 368, operation.data, 16);
    offset = static_cast<uint32_t>(position);
    maximum = static_cast<uint16_t>(limit);
    if (request.code == static_cast<uint16_t>(Operation::CacheSignRelease))
    {
        const auto released = runtime::releaseWebSignature(request.session, output + 368);
        if (released != runtime::WebReadResult::Ready && released != runtime::WebReadResult::NotFound) return publication_result(released);
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    if (request.code != static_cast<uint16_t>(Operation::CacheSignStatus) && !read) return Status::Unsupported;
    // The runtime captures operation before writing the response arena.
    return publication_result(runtime::readWebSignature(output + 368, request.session, !read, offset, maximum, output, capacity, written));
}
} // namespace hostlink::management
