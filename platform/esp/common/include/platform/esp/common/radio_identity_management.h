#pragma once
#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "hostlink/radio_identity_backup.h"
#include "platform/esp/common/radio_identity_work.h"
#include <Curve25519.h>

namespace platform::esp::common
{
inline const char* radioIdentityNamespace(RadioIdentityKind kind) { return kind == RadioIdentityKind::MeshCore ? "mc_ident" : kind == RadioIdentityKind::Meshtastic ? "chat"
                                                                                                                                                                    : nullptr; }
inline bool validateRadioIdentityPair(RadioIdentityKind kind, const uint8_t* pub, const uint8_t* priv, uint8_t* scratch, size_t capacity)
{
    if (!pub || !priv || !scratch || capacity < 32) return false;
    if (kind == RadioIdentityKind::MeshCore) return !chat::meshcore::meshcoreIsZeroBytes(priv, 32) && !chat::meshcore::meshcoreIsZeroBytes(priv + 32, 32) &&
                                                    chat::meshcore::meshcoreDerivePublicKey(priv, scratch) && !std::memcmp(pub, scratch, 32);
    return kind == RadioIdentityKind::Meshtastic && (priv[0] & 7) == 0 && (priv[31] & 0xc0) == 0x40 &&
           Curve25519::eval(scratch, priv, nullptr) && !std::memcmp(pub, scratch, 32);
}
inline IdentityRecordResult readManagedRadioIdentity(RadioIdentityKind kind, RadioIdentityWork& work, uint32_t& generation, bool& present)
{
    generation = 0;
    present = false;
    if (!work.data()) return IdentityRecordResult::StorageError;
    const auto* ns = radioIdentityNamespace(kind);
    if (!ns) return IdentityRecordResult::Invalid;
    auto loaded = readRadioIdentityRecord(ns, kind, work.data(), work.size());
    if (loaded == IdentityRecordResult::Ready)
    {
        generation = identityRecordU32(work.data() + 8);
        if (work.data()[5]) return IdentityRecordResult::Ready;
    }
    else if (loaded == IdentityRecordResult::Absent)
    {
        loaded = readLegacyRadioIdentity(ns, kind == RadioIdentityKind::MeshCore ? "pub32" : "pki_pub", kind == RadioIdentityKind::MeshCore ? "priv64" : "pki_priv", kind,
                                         work.data(), work.size(), kind == RadioIdentityKind::MeshCore ? "ver" : nullptr);
        if (loaded == IdentityRecordResult::Absent) return IdentityRecordResult::Ready;
        // Match runtime's legacy MC public reconstruction, without writing NVS.
        if (loaded == IdentityRecordResult::Ready && kind == RadioIdentityKind::MeshCore)
        {
            if (!chat::meshcore::meshcoreDerivePublicKey(work.data() + 44, work.data() + 12)) return IdentityRecordResult::Invalid;
        }
    }
    if (loaded != IdentityRecordResult::Ready) return loaded;
    if (!validateRadioIdentityPair(kind, work.data() + 12, work.data() + 44, work.data() + 112, 32)) return IdentityRecordResult::Invalid;
    present = true;
    return IdentityRecordResult::Ready;
}
template <class Identity>
chat::DeviceAuthorResult readRadioIdentityInfo(const Identity& identity, uint8_t kind_value, uint32_t author_generation, uint8_t* output, size_t capacity, size_t& written)
{
    using Result = chat::DeviceAuthorResult;
    written = 0;
    if (!output || capacity < 448 || kind_value < 1 || kind_value > 2 || !author_generation) return Result::InvalidInput;
    const auto start = reinterpret_cast<uintptr_t>(output), object = reinterpret_cast<uintptr_t>(&identity);
    if (start <= object ? object - start < capacity : start - object < sizeof(identity)) return Result::InvalidInput;
    if (!identity.isReady()) return identity.identityStorageError() ? Result::StorageError : Result::Unavailable;
    if (identity.authorGeneration() != author_generation) return Result::StaleIdentity;
    RadioIdentityWork work;
    uint32_t generation = 0;
    bool present = false;
    if (readManagedRadioIdentity(RadioIdentityKind(kind_value), work, generation, present) != IdentityRecordResult::Ready) return Result::StorageError;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !writer.unsignedInteger(kind_value) || !writer.unsignedInteger(present ? 1 : 0) || !writer.unsignedInteger(generation) ||
        !(present ? writer.binary({work.data() + 12, 32}) : writer.nil())) return Result::InvalidInput;
    written = writer.size();
    return Result::Ok;
}
template <class Identity>
chat::DeviceAuthorResult exportRadioIdentityBackup(const Identity& identity, uint8_t kind_value, uint32_t author_generation, uint8_t* output, size_t capacity, size_t& written, const uint8_t* session, const uint8_t* request)
{
    using Result = chat::DeviceAuthorResult;
    written = 0;
    if (!output || capacity < 448 || kind_value < 1 || kind_value > 2 || !author_generation || !session || !request) return Result::InvalidInput;
    const auto start = reinterpret_cast<uintptr_t>(output), object = reinterpret_cast<uintptr_t>(&identity);
    if (start <= object ? object - start < capacity : start - object < sizeof(identity)) return Result::InvalidInput;
    const auto fail = [&](Result result)
    {clearIdentityRecordWork(output,capacity);return result; };
    if (!identity.isReady()) return fail(identity.identityStorageError() ? Result::StorageError : Result::Unavailable);
    if (identity.authorGeneration() != author_generation) return fail(Result::StaleIdentity);
    RadioIdentityWork work;
    uint32_t generation = 0;
    bool present = false;
    if (readManagedRadioIdentity(RadioIdentityKind(kind_value), work, generation, present) != IdentityRecordResult::Ready) return fail(Result::StorageError);
    if (!present) return fail(Result::Unavailable);
    using namespace hostlink::management;
    std::memcpy(output, kRadioIdentityBackupDomain, sizeof(kRadioIdentityBackupDomain));
    const size_t limit = capacity - 64, begin = sizeof(kRadioIdentityBackupDomain);
    geocaching::protocol::CmpWriter writer(output + begin, limit - begin);
    if (!writer.array(10) || !writer.unsignedInteger(1) || !writer.text(kRadioIdentityBackupType) || !writer.unsignedInteger(kind_value) ||
        !writer.binary({work.data() + 12, 32}) || !writer.binary({work.data() + 44, radioIdentityPrivateBytes(RadioIdentityKind(kind_value))}) ||
        !writer.binaryHeader(64)) return fail(Result::InvalidInput);
    size_t cursor = begin + writer.size();
    if (64 > limit - cursor) return fail(Result::InvalidInput);
    identity.combinedPublicKey(output + cursor);
    cursor += 64;
    geocaching::protocol::CmpWriter tail(output + cursor, limit - cursor);
    if (!tail.unsignedInteger(author_generation) || !tail.unsignedInteger(generation) || !tail.binary({session, 16}) || !tail.binary({request, 16})) return fail(Result::InvalidInput);
    cursor += tail.size();
    const size_t payload_size = cursor - begin;
    if (payload_size > 320 || !identity.sign(output, cursor, output + limit)) return fail(Result::Unavailable);
    if (!identity.isReady() || identity.authorGeneration() != author_generation) return fail(Result::StaleIdentity);
    const size_t header = payload_size <= 255 ? 3 : 4;
    std::memmove(output + header, output + begin, payload_size);
    output[0] = 0x92;
    geocaching::protocol::CmpWriter binary(output + 1, header - 1);
    if (!binary.binaryHeader(payload_size)) return fail(Result::InvalidInput);
    geocaching::protocol::CmpWriter signature(output + header + payload_size, capacity - header - payload_size);
    if (!signature.binary({output + limit, 64})) return fail(Result::InvalidInput);
    written = header + payload_size + signature.size();
    clearIdentityRecordWork(output + written, capacity - written);
    return Result::Ok;
}
template <class Identity>
chat::DeviceIdentityRestoreResult restoreRadioIdentity(const Identity& identity, const hostlink::management::RadioIdentityRestoreView& request, uint8_t* scratch, size_t capacity)
{
    using Result = chat::DeviceIdentityRestoreResult;
    if (!scratch || capacity < 336 || request.kind < 1 || request.kind > 2 || !request.expected_author.data || request.expected_author.size != 64 ||
        request.expected_radio_generation == UINT32_MAX || (!request.expected_radio_public.data && request.expected_radio_public.size) ||
        (request.expected_radio_public.size != 0 && request.expected_radio_public.size != 32)) return Result::InvalidInput;
    const auto start = reinterpret_cast<uintptr_t>(scratch);
    for (const auto view : {request.expected_author, request.expected_radio_public, request.payload, request.signature})
    {
        if (!view.size) continue;
        const auto input = reinterpret_cast<uintptr_t>(view.data);
        if (start >= input ? start - input < view.size : input - start < capacity) return Result::InvalidInput;
    }
    struct Clear
    {
        uint8_t* work;
        size_t size;
        ~Clear() { clearIdentityRecordWork(work, size); }
    } clear{scratch, capacity};
    if (!identity.isReady()) return identity.identityStorageError() ? Result::StorageError : Result::Unavailable;
    if (identity.authorGeneration() != request.expected_author_generation) return Result::StaleIdentity;
    identity.combinedPublicKey(scratch);
    if (request.expected_author.size != 64 || std::memcmp(scratch, request.expected_author.data, 64)) return Result::StaleIdentity;
    if (!hostlink::management::verifyRadioIdentityRestoreSignature(request, scratch, capacity) ||
        !validateRadioIdentityPair(RadioIdentityKind(request.kind), request.public_key.data, request.private_key.data, scratch, capacity)) return Result::InvalidInput;
    RadioIdentityWork stored;
    uint32_t generation = 0;
    bool present = false;
    if (readManagedRadioIdentity(RadioIdentityKind(request.kind), stored, generation, present) != IdentityRecordResult::Ready) return Result::StorageError;
    if (generation != request.expected_radio_generation || present != (request.expected_radio_public.size == 32) ||
        (present && std::memcmp(stored.data() + 12, request.expected_radio_public.data, 32))) return Result::StaleRadioIdentity;
    const auto kind = RadioIdentityKind(request.kind);
    const auto saved = commitRadioIdentityRecord(
        radioIdentityNamespace(kind), kind, generation, request.public_key.data, request.private_key.data, scratch, capacity,
        [kind](const uint8_t* pub, const uint8_t* priv, uint8_t* work, size_t size)
        { return validateRadioIdentityPair(kind, pub, priv, work, size); },
        false, true);
    return saved == IdentityRecordResult::Committed ? Result::CommittedRebootRequired : saved == IdentityRecordResult::Indeterminate ? Result::IndeterminateRebootRequired
                                                                                    : saved == IdentityRecordResult::TargetChanged   ? Result::StaleRadioIdentity
                                                                                    : saved == IdentityRecordResult::Invalid         ? Result::InvalidInput
                                                                                                                                     : Result::StorageError;
}
} // namespace platform::esp::common
