#pragma once
#include "chat/ports/device_author.h"
#include "geocaching/protocol/cmp_writer.h"
#include <cstring>

namespace hostlink::management
{
// Caller holds the identity/router lock and owns a transient command buffer.
// Build directly in it, sign the domain + payload, then compact the envelope.
template <class Identity>
chat::DeviceAuthorResult exportNativeIdentityBackup(const Identity& identity, uint32_t generation,
                                                    uint8_t* output, size_t capacity, size_t& written)
{
    using Result = chat::DeviceAuthorResult;
    written = 0;
    if (!output) return Result::InvalidInput;
    const auto out = reinterpret_cast<uintptr_t>(output), object = reinterpret_cast<uintptr_t>(&identity);
    if (out <= object ? object - out < capacity : out - object < sizeof(identity)) return Result::InvalidInput;
    const auto fail = [&](Result result)
    { std::memset(output, 0, capacity); return result; };
    if (capacity < 448 || !generation) return fail(Result::InvalidInput);
    if (!identity.isReady()) return fail(identity.identityStorageError() ? Result::StorageError : Result::Unavailable);
    if (!identity.authorGeneration()) return fail(Result::StorageError);
    if (identity.authorGeneration() != generation) return fail(Result::StaleIdentity);
    static constexpr char domain[] = "TrailMate.Identity.Backup.v1";
    std::memcpy(output, domain, sizeof(domain)); // Includes domain's NUL.
    size_t cursor = sizeof(domain);
    const size_t limit = capacity - 64;
    geocaching::protocol::CmpWriter head(output + cursor, limit - cursor);
    if (!head.array(8) || !head.unsignedInteger(1) || !head.text("trailmate.identity.backup.v1") || !head.unsignedInteger(0) || !head.binaryHeader(64)) return fail(Result::InvalidInput);
    cursor += head.size();
    if (64 > limit - cursor) return fail(Result::InvalidInput);
    identity.combinedPublicKey(output + cursor);
    cursor += 64;
    geocaching::protocol::CmpWriter secret(output + cursor, limit - cursor);
    if (!secret.binaryHeader(96)) return fail(Result::InvalidInput);
    cursor += secret.size();
    if (96 > limit - cursor || !identity.copyNativePrivateKey(generation, output + cursor, 96)) return fail(Result::StaleIdentity);
    cursor += 96;
    geocaching::protocol::CmpWriter tail(output + cursor, limit - cursor);
    if (!tail.binary({identity.identityHash(), 16}) || !tail.binary({identity.destinationHash(), 16}) || !tail.unsignedInteger(generation)) return fail(Result::InvalidInput);
    cursor += tail.size();
    const size_t payload = cursor - sizeof(domain);
    if (payload > 255 || !identity.sign(output, cursor, output + limit)) return fail(Result::Unavailable);
    if (!identity.isReady() || identity.authorGeneration() != generation) return fail(Result::StaleIdentity);
    std::memmove(output + 3, output + sizeof(domain), payload);
    output[0] = 0x92;
    output[1] = 0xc4;
    output[2] = static_cast<uint8_t>(payload);
    geocaching::protocol::CmpWriter signature(output + 3 + payload, capacity - 3 - payload);
    if (!signature.binary({output + limit, 64})) return fail(Result::InvalidInput);
    written = 3 + payload + signature.size();
    std::memset(output + written, 0, capacity - written);
    return Result::Ok;
}
} // namespace hostlink::management
