#pragma once
#include "geocaching/storage/publication_confirmation.h"
#include "hostlink/geocache_confirmation_request.h"
#include "platform/esp/arduino_common/hostlink/web_config_upload.h"
#include "platform/esp/arduino_common/hostlink/web_publication_prepare.h"
#include "platform/memory/psram_ptr.h"
namespace hostlink::management
{
inline Status cache_confirmation(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || capacity < 448) return Status::Internal;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    if (request.code == static_cast<uint16_t>(Operation::CacheConfirmBegin))
    {
        GeocacheConfirmationRequest decoded;
        const auto parsed = parseGeocacheConfirmationRequest(request.body, request.body_size, decoded);
        if (parsed != Status::Ok) return parsed;
        // Preserve proof and bindings before shared response scratch is reused.
        auto proof = ::platform::memory::PsramPtr<std::array<uint8_t, 384>>(::platform::memory::createPsram<std::array<uint8_t, 384>>());
        size_t proof_size = 0;
        if (!proof) return Status::StorageUnavailable;
        ::geocaching::protocol::CmpReader raw(decoded.proof);
        size_t count = 0;
        uint64_t version = 0;
        ::geocaching::ByteView directory, epoch;
        if (!raw.array(count, 8) || count != 8 || !raw.unsignedInteger(version) || version != 1 || !raw.binary(directory, 16) || directory.size != 16 ||
            !raw.binary(epoch, 16) || epoch.size != 16 || std::memcmp(epoch.data, decoded.epoch.data, 16)) return Status::InvalidField;
        if (!::geocaching::storage::encodePublicationConfirmation({decoded.proof, decoded.signature, decoded.directory_public}, proof->data(), proof->size(), proof_size)) return Status::InvalidField;
        std::memmove(output + 320, decoded.transfer.data, 16);
        std::memmove(output + 336, decoded.hash.data, 32);
        const auto expected_generation = decoded.author_generation;
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        uint32_t actual = 0;
        if (!adapter) return Status::Busy;
        const auto identity = adapter->readDeviceAuthorKey(output + 384, actual);
        if (identity != chat::DeviceAuthorResult::Ok) return identity == chat::DeviceAuthorResult::StorageError ? Status::StorageUnavailable : Status::Busy;
        if (actual != expected_generation) return Status::StaleIdentity;
        web_config::Metadata metadata;
        if (!web_config::load(metadata, output)) return Status::Expired;
        if (std::memcmp(metadata.owner, request.session, 16)) return Status::Unauthorized;
        if (metadata.kind != 6 || !metadata.phase || std::memcmp(metadata.id, output + 320, 16) || std::memcmp(metadata.hash, output + 336, 32)) return Status::Conflict;
        const auto queued = runtime::queueWebConfirmation(output + 320, request.session, output + 336, metadata.total, output + 384, actual, proof->data(), proof_size);
        if (queued != runtime::WebReadResult::Ready) return publication_result(queued);
        const auto result = runtime::readWebConfirmation(output + 320, request.session, output, 320, written);
        return result == runtime::WebReadResult::Ready ? web_config::finishConfirmedUpload(request, output, capacity, written) : publication_result(result);
    }
    ::geocaching::protocol::CmpReader body({request.body, request.body_size});
    size_t count = 0;
    ::geocaching::ByteView transfer;
    if (!body.array(count, 1) || count != 1 || !body.binary(transfer, 16) || transfer.size != 16 || !body.finished()) return Status::InvalidField;
    std::memmove(output + 320, transfer.data, 16);
    if (request.code == static_cast<uint16_t>(Operation::CacheConfirmStatus))
    {
        const auto result = runtime::readWebConfirmation(output + 320, request.session, output, 320, written);
        return result == runtime::WebReadResult::Ready ? web_config::finishConfirmedUpload(request, output, capacity, written) : publication_result(result);
    }
    if (request.code == static_cast<uint16_t>(Operation::CacheConfirmRelease))
    {
        const auto released = runtime::releaseWebConfirmation(request.session, output + 320);
        if (released != runtime::WebReadResult::Ready && released != runtime::WebReadResult::NotFound) return publication_result(released);
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    return Status::Unsupported;
}
} // namespace hostlink::management
