#pragma once
#include "platform/esp/common/identity_atomic_record.h"
#include <atomic>

namespace platform::esp::common
{
enum class RadioIdentityKind : uint8_t
{
    MeshCore = 1,
    Meshtastic = 2
};
constexpr size_t kRadioIdentityRecordBytes = 112;
constexpr size_t kRadioIdentityWorkBytes = kRadioIdentityRecordBytes + 32;
constexpr const char* kRadioIdentityRecordKey = "native_rf1";
// One byte, shared by both native RF writers. Restoration leaves state=2 until
// reboot, closing the gap between NVS commit and the scheduled restart.
inline std::atomic<uint8_t> radio_identity_write_state{0};
static_assert(sizeof(radio_identity_write_state) == 1);
class RadioIdentityWriteGuard
{
  public:
    RadioIdentityWriteGuard()
    {
        uint8_t expected = 0;
        locked_ = radio_identity_write_state.compare_exchange_strong(expected, 1, std::memory_order_acquire);
    }
    ~RadioIdentityWriteGuard()
    {
        if (!locked_) return;
        uint8_t expected = 1;
        radio_identity_write_state.compare_exchange_strong(expected, 0, std::memory_order_release);
    }
    bool locked() const { return locked_; }
    void requireReboot()
    {
        if (locked_) radio_identity_write_state.store(2, std::memory_order_release);
    }
    RadioIdentityWriteGuard(const RadioIdentityWriteGuard&) = delete;
    RadioIdentityWriteGuard& operator=(const RadioIdentityWriteGuard&) = delete;

  private:
    bool locked_ = false;
};
inline size_t radioIdentityPrivateBytes(RadioIdentityKind kind)
{
    return kind == RadioIdentityKind::MeshCore ? 64 : kind == RadioIdentityKind::Meshtastic ? 32
                                                                                            : 0;
}
inline bool radioIdentityRecordValid(const uint8_t* record, size_t size, RadioIdentityKind kind)
{
    const auto private_size = radioIdentityPrivateBytes(kind);
    if (!record || size != kRadioIdentityRecordBytes || !private_size || std::memcmp(record, "TMRF", 4) ||
        record[4] != 1 || record[5] > 1 || record[6] != uint8_t(kind) || record[7] != private_size ||
        !identityRecordU32(record + 8) || identityRecordU32(record + 108) != identityRecordCrc(record, 108)) return false;
    // A cleared record is a durable tombstone. It suppresses stale legacy keys
    // without deleting them in a separate transaction or resurrecting them.
    const size_t first_zero = record[5] ? 12 : 44 + private_size;
    for (size_t index = first_zero; index < 108; ++index)
        if (record[index]) return false;
    return true;
}
inline IdentityRecordResult readRadioIdentityRecord(const char* ns, RadioIdentityKind kind, uint8_t* output, size_t capacity)
{
    if (!ns || !output || capacity < kRadioIdentityRecordBytes || !radioIdentityPrivateBytes(kind)) return IdentityRecordResult::Invalid;
    clearIdentityRecordWork(output, kRadioIdentityRecordBytes);
    nvs_handle_t handle = 0;
    auto status = nvs_open(ns, NVS_READONLY, &handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) return IdentityRecordResult::Absent;
    if (status != ESP_OK) return IdentityRecordResult::StorageError;
    size_t size = kRadioIdentityRecordBytes;
    status = nvs_get_blob(handle, kRadioIdentityRecordKey, output, &size);
    nvs_close(handle);
    if (status == ESP_ERR_NVS_NOT_FOUND) return IdentityRecordResult::Absent;
    if (status != ESP_OK || !radioIdentityRecordValid(output, size, kind))
    {
        clearIdentityRecordWork(output, kRadioIdentityRecordBytes);
        return status == ESP_OK ? IdentityRecordResult::Invalid : IdentityRecordResult::StorageError;
    }
    return IdentityRecordResult::Ready;
}
inline bool radioIdentityLegacyAbsent(const char* ns, const char* public_key, const char* private_key)
{
    if (!ns || !public_key || !private_key) return false;
    nvs_handle_t handle = 0;
    const auto opened = nvs_open(ns, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if (opened != ESP_OK) return false;
    size_t size = 0;
    const auto pub = nvs_get_blob(handle, public_key, nullptr, &size);
    size = 0;
    const auto priv = nvs_get_blob(handle, private_key, nullptr, &size);
    nvs_close(handle);
    return pub == ESP_ERR_NVS_NOT_FOUND && priv == ESP_ERR_NVS_NOT_FOUND;
}
// Read old pairs directly into caller PSRAM; no vector or temporary secret copy.
// MeshCore can reconstruct a missing legacy public key from the expanded key;
// its caller validates/reconstructs before use. Meshtastic requires both keys.
inline IdentityRecordResult readLegacyRadioIdentity(const char* ns, const char* public_key, const char* private_key,
                                                    RadioIdentityKind kind, uint8_t* output, size_t capacity,
                                                    const char* version_key = nullptr)
{
    const auto private_size = radioIdentityPrivateBytes(kind);
    if (!ns || !public_key || !private_key || !output || capacity < kRadioIdentityRecordBytes || !private_size)
        return IdentityRecordResult::Invalid;
    clearIdentityRecordWork(output, kRadioIdentityRecordBytes);
    nvs_handle_t handle = 0;
    const auto opened = nvs_open(ns, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return IdentityRecordResult::Absent;
    if (opened != ESP_OK) return IdentityRecordResult::StorageError;
    struct Close
    {
        nvs_handle_t handle;
        ~Close() { nvs_close(handle); }
    } close{handle};
    size_t pub_size = 0, priv_size = 0;
    const auto pub = nvs_get_blob(handle, public_key, nullptr, &pub_size);
    const auto priv = nvs_get_blob(handle, private_key, nullptr, &priv_size);
    if (pub == ESP_ERR_NVS_NOT_FOUND && priv == ESP_ERR_NVS_NOT_FOUND) return IdentityRecordResult::Absent;
    if (priv != ESP_OK || priv_size != private_size || (pub == ESP_OK && pub_size != 32) ||
        (pub != ESP_OK && (pub != ESP_ERR_NVS_NOT_FOUND || kind != RadioIdentityKind::MeshCore))) return IdentityRecordResult::Invalid;
    if (version_key)
    {
        uint8_t version = 0;
        const auto read = nvs_get_u8(handle, version_key, &version);
        if ((read == ESP_OK && version != 1) || (read != ESP_OK && read != ESP_ERR_NVS_NOT_FOUND)) return IdentityRecordResult::Invalid;
    }
    if (nvs_get_blob(handle, private_key, output + 44, &priv_size) != ESP_OK || priv_size != private_size ||
        (pub == ESP_OK && (nvs_get_blob(handle, public_key, output + 12, &pub_size) != ESP_OK || pub_size != 32)))
    {
        clearIdentityRecordWork(output, kRadioIdentityRecordBytes);
        return IdentityRecordResult::StorageError;
    }
    return IdentityRecordResult::Ready;
}
// Expected generation is RF-local; it never changes the Reticulum account.
// No automatic large buffer. The owner serializes access and provides scratch.
// Validate must prove the protocol-specific public/private correspondence.
template <typename Validate>
IdentityRecordResult commitRadioIdentityRecord(const char* ns, RadioIdentityKind kind, uint32_t expected_generation,
                                               const uint8_t* public_key, const uint8_t* private_key,
                                               uint8_t* work, size_t capacity, Validate validate, bool clear = false,
                                               bool require_reboot = false)
{
    const auto private_size = radioIdentityPrivateBytes(kind);
    if (!ns || !private_size || !work || capacity < kRadioIdentityWorkBytes ||
        (!clear && (!public_key || !private_key)) || expected_generation == UINT32_MAX) return IdentityRecordResult::Invalid;
    const auto start = reinterpret_cast<uintptr_t>(work);
    if (!clear)
    {
        const auto pub = reinterpret_cast<uintptr_t>(public_key), priv = reinterpret_cast<uintptr_t>(private_key);
        if ((start >= pub ? start - pub < 32 : pub - start < kRadioIdentityWorkBytes) ||
            (start >= priv ? start - priv < private_size : priv - start < kRadioIdentityWorkBytes)) return IdentityRecordResult::Invalid;
    }
    struct Clear
    {
        uint8_t* work;
        ~Clear() { clearIdentityRecordWork(work, kRadioIdentityWorkBytes); }
    } wipe{work};
    RadioIdentityWriteGuard guard;
    if (!guard.locked()) return IdentityRecordResult::TargetChanged;
    if (!clear && !validate(public_key, private_key, work + kRadioIdentityRecordBytes, 32)) return IdentityRecordResult::Invalid;
    const auto prior = readRadioIdentityRecord(ns, kind, work, capacity);
    if (prior != IdentityRecordResult::Ready && prior != IdentityRecordResult::Absent) return prior;
    const auto generation = prior == IdentityRecordResult::Ready ? identityRecordU32(work + 8) : 0;
    if (generation != expected_generation) return IdentityRecordResult::TargetChanged;
    clearIdentityRecordWork(work, kRadioIdentityRecordBytes);
    std::memcpy(work, "TMRF", 4);
    work[4] = 1;
    work[5] = clear ? 1 : 0;
    work[6] = uint8_t(kind);
    work[7] = uint8_t(private_size);
    identityRecordPutU32(work + 8, generation + 1);
    if (!clear)
    {
        std::memcpy(work + 12, public_key, 32);
        std::memcpy(work + 44, private_key, private_size);
    }
    identityRecordPutU32(work + 108, identityRecordCrc(work, 108));
    nvs_handle_t handle = 0;
    if (nvs_open(ns, NVS_READWRITE, &handle) != ESP_OK) return IdentityRecordResult::StorageError;
    const auto set = nvs_set_blob(handle, kRadioIdentityRecordKey, work, kRadioIdentityRecordBytes);
    const auto committed = set == ESP_OK ? nvs_commit(handle) : set;
    nvs_close(handle);
    if (require_reboot) guard.requireReboot(); // Any attempted write may have committed.
    return set == ESP_OK && committed == ESP_OK ? IdentityRecordResult::Committed : IdentityRecordResult::Indeterminate;
}
} // namespace platform::esp::common
