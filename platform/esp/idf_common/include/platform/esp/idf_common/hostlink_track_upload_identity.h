#pragma once
#include "chat/ports/device_author.h"
#include "hostlink/management_envelope.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"
#include "platform/esp/idf_common/hostlink_track_upload_runtime.h"
#include <cstring>
namespace hostlink::management::upload_runtime
{
template <class Adapter>
Status readUploadAuthor(Adapter& adapter, uint8_t* public_key, size_t capacity, uint8_t* author_hash, uint32_t& generation)
{
    generation = 0;
    if (!public_key || capacity < 64 || !author_hash) return Status::Internal;
    const auto a = reinterpret_cast<uintptr_t>(public_key), b = reinterpret_cast<uintptr_t>(author_hash);
    if (a <= b ? b - a < 64 : a - b < 32) return Status::Internal;
    std::memset(author_hash, 0, 32);
    std::memset(public_key, 0, 64);
    uint32_t actual = 0;
    const auto result = adapter.readDeviceAuthorKey(public_key, actual);
    if (result != chat::DeviceAuthorResult::Ok)
        return result == chat::DeviceAuthorResult::StorageError ? Status::StorageUnavailable : result == chat::DeviceAuthorResult::Unsupported ? Status::Unsupported
                                                                                                                                               : Status::Busy;
    uint8_t bits = 0;
    for (unsigned i = 0; i < 64; ++i) bits |= public_key[i];
    if (!actual || !bits) return Status::StaleIdentity;
#if MBEDTLS_VERSION_NUMBER < 0x03000000
    const int hashed = mbedtls_sha256_ret(public_key, 64, author_hash, 0);
#else
    const int hashed = mbedtls_sha256(public_key, 64, author_hash, 0);
#endif
    if (hashed)
    {
        std::memset(author_hash, 0, 32);
        return Status::StorageUnavailable;
    }
    generation = actual;
    return Status::Ok;
}
template <class Adapter>
Status processAuthenticatedUpload(Adapter& adapter, const EnvelopeView& request, uint64_t now,
                                  uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    if (!scratch || capacity < 448 || !routeTrackUpload(request)) return Status::InvalidField;
    auto* public_key = scratch + capacity - 64;
    const auto a = reinterpret_cast<uintptr_t>(request.body), b = reinterpret_cast<uintptr_t>(public_key);
    if (a <= b ? b - a < request.body_size : a - b < 64) return Status::Internal;
    uint8_t author[32];
    uint32_t generation = 0;
    const auto identity = readUploadAuthor(adapter, public_key, 64, author, generation);
    if (identity != Status::Ok) return identity;
    return processUploadRequest(request, generation, author, now, scratch, capacity, written);
}
} // namespace hostlink::management::upload_runtime
