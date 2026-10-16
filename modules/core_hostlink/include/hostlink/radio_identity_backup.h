#pragma once
#include "chat/ports/device_author.h"
#include "hostlink/identity_restore_service.h"
#include "hostlink/identity_signing_key_validation.h"

namespace hostlink::management
{
inline constexpr char kRadioIdentityBackupDomain[] = "TrailMate.Radio.Identity.Backup.v1";
inline constexpr char kRadioIdentityBackupType[] = "trailmate.radio.identity.backup.v1";
struct RadioIdentityRestoreView
{
    uint32_t expected_author_generation = 0, expected_radio_generation = 0, source_author_generation = 0, source_radio_generation = 0;
    uint8_t kind = 0;
    geocaching::ByteView expected_author{}, expected_radio_public{}, payload{}, signature{}, public_key{}, private_key{}, source_author{}, export_session{}, export_request{};
};
inline Status parseRadioIdentityBackupPayload(geocaching::ByteView bytes, RadioIdentityRestoreView& parsed)
{
    if (!bytes.data || bytes.size > 320) return Status::InvalidField;
    geocaching::protocol::CmpReader payload(bytes);
    std::string_view type;
    size_t fields = 0;
    uint64_t value = 0;
    if (!payload.array(fields, 10) || fields != 10 || !payload.unsignedInteger(value) || value != 1 ||
        !payload.text(type, 40) || type != kRadioIdentityBackupType || !payload.unsignedInteger(value) || value < 1 || value > 2) return Status::InvalidField;
    parsed.kind = uint8_t(value);
    if (!payload.binary(parsed.public_key, 32) || parsed.public_key.size != 32 ||
        !payload.binary(parsed.private_key, 64) || parsed.private_key.size != (parsed.kind == 1 ? 64U : 32U) ||
        !payload.binary(parsed.source_author, 64) || parsed.source_author.size != 64 ||
        !payload.unsignedInteger(value) || !value || value > UINT32_MAX) return Status::InvalidField;
    parsed.source_author_generation = uint32_t(value);
    if (!payload.unsignedInteger(value) || value > UINT32_MAX || !payload.binary(parsed.export_session, 16) || parsed.export_session.size != 16 ||
        !payload.binary(parsed.export_request, 16) || parsed.export_request.size != 16 || !payload.finished()) return Status::InvalidField;
    parsed.source_radio_generation = uint32_t(value);
    parsed.payload = bytes;
    return Status::Ok;
}
inline bool radioIdentityBackupPayloadMatches(const RadioIdentityRestoreView& request)
{
    RadioIdentityRestoreView decoded;
    if (parseRadioIdentityBackupPayload(request.payload, decoded) != Status::Ok) return false;
    const auto equal = [](geocaching::ByteView a, geocaching::ByteView b)
    { return a.data && b.data && a.size == b.size && !std::memcmp(a.data, b.data, a.size); };
    return request.kind == decoded.kind && request.source_author_generation == decoded.source_author_generation && request.source_radio_generation == decoded.source_radio_generation &&
           equal(request.public_key, decoded.public_key) && equal(request.private_key, decoded.private_key) && equal(request.source_author, decoded.source_author) &&
           equal(request.export_session, decoded.export_session) && equal(request.export_request, decoded.export_request);
}
inline Status parseRadioIdentityRestoreRequest(geocaching::ByteView input, RadioIdentityRestoreView& output)
{
    output = {};
    if (!input.data || input.size > 448) return Status::InvalidField;
    geocaching::protocol::CmpReader reader(input);
    RadioIdentityRestoreView parsed;
    size_t fields = 0;
    uint64_t value = 0;
    if (!reader.array(fields, 7) || fields != 7 || !reader.unsignedInteger(value) || !value || value > UINT32_MAX) return Status::InvalidField;
    parsed.expected_author_generation = uint32_t(value);
    if (!reader.binary(parsed.expected_author, 64) || parsed.expected_author.size != 64 ||
        !reader.binary(parsed.payload, 320) || !reader.binary(parsed.signature, 64) || parsed.signature.size != 64 ||
        !reader.unsignedInteger(value) || value >= UINT32_MAX) return Status::InvalidField;
    parsed.expected_radio_generation = uint32_t(value);
    if (reader.position() >= input.size) return Status::InvalidField;
    if (input.data[reader.position()] == 0xc0)
    {
        if (!reader.nil()) return Status::InvalidField;
    }
    else if (!reader.binary(parsed.expected_radio_public, 32) || parsed.expected_radio_public.size != 32) return Status::InvalidField;
    if (reader.position() + 1 != input.size) return Status::InvalidField;
    if (input.data[reader.position()] == 0xc2) return Status::Unauthorized;
    if (input.data[reader.position()] != 0xc3) return Status::InvalidField;
    if (parseRadioIdentityBackupPayload(parsed.payload, parsed) != Status::Ok) return Status::InvalidField;
    output = parsed;
    return Status::Ok;
}
inline bool verifyRadioIdentityRestoreSignature(const RadioIdentityRestoreView& request, uint8_t* work, size_t capacity)
{
    const size_t length = sizeof(kRadioIdentityBackupDomain) + request.payload.size;
    if (!work || !request.payload.data || request.payload.size > 320 || capacity < length || !request.source_author.data || request.source_author.size != 64 || !request.signature.data || request.signature.size != 64 ||
        !radioIdentityBackupPayloadMatches(request)) return false;
    const auto start = reinterpret_cast<uintptr_t>(work);
    for (const auto view : {request.payload, request.source_author, request.signature})
    {
        const auto input = reinterpret_cast<uintptr_t>(view.data);
        if (start >= input ? start - input < view.size : input - start < length) return false;
    }
    std::memcpy(work, kRadioIdentityBackupDomain, sizeof(kRadioIdentityBackupDomain));
    std::memcpy(work + sizeof(kRadioIdentityBackupDomain), request.payload.data, request.payload.size);
    const bool valid = ed25519_verify(request.signature.data, work, length, request.source_author.data + 32) != 0;
    volatile uint8_t* clear = work;
    for (size_t i = 0; i < length; ++i) clear[i] = 0;
    return valid;
}
struct RadioIdentityRestoreWork
{
    RadioIdentityRestoreView request;
    uint8_t crypto[336]{};
};
static_assert(sizeof(RadioIdentityRestoreWork) <= 512, "RF restore scratch must be transient and bounded");
template <class Adapter>
Status processRadioIdentityRestore(Adapter& adapter, const EnvelopeView& input, const IdentityRestoreActivity& activity,
                                   RadioIdentityRestoreWork& work, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    struct Clear
    {
        RadioIdentityRestoreWork& work;
        ~Clear()
        {
            volatile uint8_t* bytes = reinterpret_cast<volatile uint8_t*>(&work);
            for (size_t i = 0; i < sizeof(work); ++i) bytes[i] = 0;
        }
    } clear{work};
    if (!input.session || input.code != uint16_t(Operation::RadioIdentityBackupRestore) || !output || capacity < 48) return Status::InvalidField;
    if (activity.busy()) return Status::Busy;
    const auto decoded = parseRadioIdentityRestoreRequest({input.body, input.body_size}, work.request);
    if (decoded != Status::Ok) return decoded;
    const auto result = adapter.restoreRadioIdentity(work.request, work.crypto, sizeof(work.crypto));
    using Result = chat::DeviceIdentityRestoreResult;
    if (result == Result::CommittedRebootRequired || result == Result::IndeterminateRebootRequired)
    {
        // The response can overwrite the input command only after storage is done.
        std::memcpy(work.crypto, work.request.public_key.data, 32);
        geocaching::protocol::CmpWriter writer(output, capacity);
        if (!writer.array(4) || !writer.unsignedInteger(result == Result::CommittedRebootRequired ? 0 : 1) ||
            !writer.unsignedInteger(work.request.kind) || !writer.unsignedInteger(work.request.expected_radio_generation + 1) ||
            !writer.binary({work.crypto, 32})) return Status::Internal;
        written = writer.size();
        return Status::Ok;
    }
    return result == Result::StaleIdentity ? Status::StaleIdentity : result == Result::StaleRadioIdentity ? Status::Conflict
                                                                 : result == Result::InvalidInput         ? Status::InvalidField
                                                                 : result == Result::StorageError         ? Status::StorageUnavailable
                                                                 : result == Result::Unsupported          ? Status::Unsupported
                                                                                                          : Status::Busy;
}
template <class Adapter>
Status radioIdentityReadOperation(const Adapter& adapter, const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    const bool exporting = request.code == uint16_t(Operation::RadioIdentityBackupExport);
    if (!request.session || (!exporting && request.code != uint16_t(Operation::RadioIdentityInfo)) || !output || capacity < 448) return Status::InvalidField;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t generation = 0, kind = 0;
    if (!reader.array(fields, 3) || fields != (exporting ? 3U : 2U) || !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX ||
        !reader.unsignedInteger(kind) || kind < 1 || kind > 2) return Status::InvalidField;
    if (exporting)
    {
        if (reader.position() + 1 != request.body_size) return Status::InvalidField;
        const auto consent = request.body[reader.position()];
        if (consent == 0xc2) return Status::Unauthorized;
        if (consent != 0xc3) return Status::InvalidField;
    }
    else if (!reader.finished()) return Status::InvalidField;
    if (exporting)
    {
        if (!request.request) return Status::InvalidField;
        std::memmove(output + capacity - 32, request.session, 16);
        std::memmove(output + capacity - 16, request.request, 16);
    }
    const auto result = exporting ? adapter.exportRadioIdentityBackup(uint8_t(kind), uint32_t(generation), output, capacity, written, output + capacity - 32, output + capacity - 16) : adapter.readRadioIdentityInfo(uint8_t(kind), uint32_t(generation), output, capacity, written);
    using Result = chat::DeviceAuthorResult;
    return result == Result::Ok ? Status::Ok : result == Result::StaleIdentity ? Status::StaleIdentity
                                           : result == Result::StorageError    ? Status::StorageUnavailable
                                           : result == Result::Unsupported     ? Status::Unsupported
                                           : result == Result::InvalidInput    ? Status::InvalidField
                                                                               : Status::Busy;
}
} // namespace hostlink::management
