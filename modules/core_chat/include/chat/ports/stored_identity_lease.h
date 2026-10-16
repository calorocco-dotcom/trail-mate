#pragma once
#include "chat/ports/device_author.h"
#include "chat/ports/device_identity_restore.h"
#include <atomic>
#include <new>

namespace chat
{
// The caller holds the identity/router lock for the entire lease. Memory must
// provide allocate(size) and release(pointer); no fallback allocation is used.
template <class Identity, class Memory>
class StoredIdentityLease
{
  public:
    StoredIdentityLease() : StoredIdentityLease([](Identity& identity)
                                                { return identity.initExisting(); }) {}
    template <class Initialize>
    explicit StoredIdentityLease(Initialize initialize)
    {
        if (rebootRequired()) return;
        auto* memory = Memory::allocate(sizeof(Identity));
        if (!memory) return;
        identity_ = new (memory) Identity;
        if (!initialize(*identity_)) return;
        result_ = identity_->authorGeneration() ? DeviceAuthorResult::Ok : DeviceAuthorResult::StorageError;
    }
    ~StoredIdentityLease()
    {
        if (!identity_) return;
        identity_->~Identity();
        auto* bytes = reinterpret_cast<volatile uint8_t*>(identity_);
        for (size_t index = 0; index < sizeof(Identity); ++index) bytes[index] = 0;
        Memory::release(identity_);
    }
    StoredIdentityLease(const StoredIdentityLease&) = delete;
    StoredIdentityLease& operator=(const StoredIdentityLease&) = delete;
    DeviceAuthorResult result() const
    {
        if (rebootRequired()) return DeviceAuthorResult::Unavailable;
        return identity_ && identity_->identityStorageError() ? DeviceAuthorResult::StorageError : result_;
    }
    Identity* get() const { return result() == DeviceAuthorResult::Ok ? identity_ : nullptr; }
    static bool rebootRequired() { return reboot_required_.load(std::memory_order_acquire); }
    static void noteRestore(DeviceIdentityRestoreResult result)
    {
        if (result == DeviceIdentityRestoreResult::CommittedRebootRequired ||
            result == DeviceIdentityRestoreResult::IndeterminateRebootRequired)
            reboot_required_.store(true, std::memory_order_release);
    }
    DeviceAuthorResult read(uint8_t* public_key, uint32_t& generation) const
    {
        generation = 0;
        if (!public_key) return DeviceAuthorResult::InvalidInput;
        std::memset(public_key, 0, 64);
        if (result() != DeviceAuthorResult::Ok) return result();
        identity_->combinedPublicKey(public_key);
        generation = identity_->authorGeneration();
        return DeviceAuthorResult::Ok;
    }
    DeviceAuthorResult prove(const uint8_t* session, const uint8_t* challenge, uint32_t expected,
                             uint8_t* workspace, size_t capacity, uint8_t* public_key,
                             uint32_t& generation, uint8_t* signature) const
    {
        if (!session || !challenge || !expected || !workspace || !signature) return DeviceAuthorResult::InvalidInput;
        std::memset(signature, 0, 64);
        const auto status = read(public_key, generation);
        if (status != DeviceAuthorResult::Ok) return status;
        if (generation != expected) return DeviceAuthorResult::StaleIdentity;
        size_t written = 0;
        if (!deviceAuthorProofMessage(session, challenge, public_key, generation, workspace, capacity, written))
            return DeviceAuthorResult::InvalidInput;
        return identity_->sign(workspace, written, signature) ? DeviceAuthorResult::Ok : DeviceAuthorResult::Unavailable;
    }

  private:
    Identity* identity_ = nullptr;
    DeviceAuthorResult result_ = DeviceAuthorResult::Unavailable;
    static inline std::atomic<bool> reboot_required_{false};
};
} // namespace chat
