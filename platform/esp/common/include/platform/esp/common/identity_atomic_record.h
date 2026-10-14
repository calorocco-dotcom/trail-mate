#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <nvs.h>

namespace platform::esp::common
{
constexpr size_t kIdentityRecordBytes = 176;
constexpr size_t kIdentityRecordWorkBytes = kIdentityRecordBytes + 32;
enum class IdentityRecordResult
{
    Absent,
    Ready,
    Invalid,
    StorageError,
    TargetChanged,
    Committed,
    Indeterminate
};
inline uint32_t identityRecordU32(const uint8_t* data)
{
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}
inline void identityRecordPutU32(uint8_t* data, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) data[i] = uint8_t(value >> (8 * i));
}
inline uint32_t identityRecordCrc(const uint8_t* data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
inline void clearIdentityRecordWork(uint8_t* work, size_t size)
{
    volatile uint8_t* target = work;
    if (target)
        for (size_t i = 0; i < size; ++i) target[i] = 0;
}
// CRC detects damaged record metadata. It is not authentication; key pairing
// and signed USB import authorization are separate requirements.
inline bool identityRecordHeaderValid(const uint8_t* record, size_t size)
{
    return record && size == kIdentityRecordBytes && !std::memcmp(record, "TMID", 4) &&
           record[4] == 1 && record[5] == 0 && record[6] == 0 && record[7] == 0 &&
           identityRecordU32(record + 8) != 0 &&
           identityRecordU32(record + 172) == identityRecordCrc(record, 172);
}
inline IdentityRecordResult readIdentityRecord(uint8_t* output, size_t capacity)
{
    if (!output || capacity < kIdentityRecordBytes) return IdentityRecordResult::Invalid;
    nvs_handle_t handle = 0;
    auto status = nvs_open("lxmf_ident", NVS_READONLY, &handle);
    if (status == ESP_ERR_NVS_NOT_FOUND)
    {
        clearIdentityRecordWork(output, kIdentityRecordBytes);
        return IdentityRecordResult::Absent;
    }
    if (status != ESP_OK)
    {
        clearIdentityRecordWork(output, kIdentityRecordBytes);
        return IdentityRecordResult::StorageError;
    }
    size_t size = kIdentityRecordBytes;
    status = nvs_get_blob(handle, "native_v1", output, &size);
    nvs_close(handle);
    if (status == ESP_ERR_NVS_NOT_FOUND)
    {
        clearIdentityRecordWork(output, kIdentityRecordBytes);
        return IdentityRecordResult::Absent;
    }
    if (status != ESP_OK || !identityRecordHeaderValid(output, size))
    {
        clearIdentityRecordWork(output, kIdentityRecordBytes);
        return status == ESP_OK ? IdentityRecordResult::Invalid : IdentityRecordResult::StorageError;
    }
    return IdentityRecordResult::Ready;
}
// Caller serializes this with the identity owner lock and passes its live
// generation. Never activate keys after Indeterminate; reconcile on reboot.
// One blob contains generation, public64 and native private96; legacy keys are
// neither overwritten nor removed. Work is caller-owned and wiped on exit.
template <typename Validate>
IdentityRecordResult commitIdentityRecord(uint32_t expected_generation, uint32_t live_generation,
                                          const uint8_t* public_key, const uint8_t* private_key,
                                          uint8_t* work, size_t capacity, Validate validate)
{
    if (!work || capacity < kIdentityRecordWorkBytes || !public_key || !private_key) return IdentityRecordResult::Invalid;
    const auto start = reinterpret_cast<uintptr_t>(work);
    const auto pub = reinterpret_cast<uintptr_t>(public_key), priv = reinterpret_cast<uintptr_t>(private_key);
    if ((start >= pub ? start - pub < 64 : pub - start < kIdentityRecordWorkBytes) ||
        (start >= priv ? start - priv < 96 : priv - start < kIdentityRecordWorkBytes)) return IdentityRecordResult::Invalid;
    struct Clear
    {
        uint8_t* work;
        ~Clear() { clearIdentityRecordWork(work, kIdentityRecordWorkBytes); }
    } clear{work};
    if (!expected_generation || expected_generation != live_generation) return IdentityRecordResult::TargetChanged;
    if (live_generation == UINT32_MAX || !validate(public_key, private_key, work + kIdentityRecordBytes, 32)) return IdentityRecordResult::Invalid;
    const auto stored = readIdentityRecord(work, capacity);
    if (stored != IdentityRecordResult::Absent && stored != IdentityRecordResult::Ready) return stored;
    if (stored == IdentityRecordResult::Ready && identityRecordU32(work + 8) > live_generation) return IdentityRecordResult::TargetChanged;
    std::memcpy(work, "TMID", 4);
    work[4] = 1;
    work[5] = work[6] = work[7] = 0;
    identityRecordPutU32(work + 8, live_generation + 1);
    std::memcpy(work + 12, public_key, 64);
    std::memcpy(work + 76, private_key, 96);
    identityRecordPutU32(work + 172, identityRecordCrc(work, 172));
    nvs_handle_t handle = 0;
    if (nvs_open("lxmf_ident", NVS_READWRITE, &handle) != ESP_OK) return IdentityRecordResult::StorageError;
    const auto set = nvs_set_blob(handle, "native_v1", work, kIdentityRecordBytes);
    const auto commit = set == ESP_OK ? nvs_commit(handle) : set;
    nvs_close(handle);
    // A reported write error does not prove that the old record survived.
    return set == ESP_OK && commit == ESP_OK ? IdentityRecordResult::Committed : IdentityRecordResult::Indeterminate;
}
} // namespace platform::esp::common
