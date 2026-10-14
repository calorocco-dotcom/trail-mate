#include "chat/ports/stored_identity_lease.h"
#include <array>
#include <cassert>
#include <cstdlib>
struct Identity
{
    static inline bool exists = true, bad = false;
    static inline unsigned loads = 0, signs = 0;
    std::array<uint8_t, 200> secret{};
    bool initExisting()
    {
        ++loads;
        secret.fill(42);
        return exists && !bad;
    }
    bool identityStorageError() const { return bad; }
    uint32_t authorGeneration() const { return 7; }
    void combinedPublicKey(uint8_t* output) const { std::memset(output, 3, 64); }
    bool sign(const uint8_t* message, size_t size, uint8_t* signature) const
    {
        ++signs;
        assert(size > 100 && message[0] == 't');
        std::memset(signature, 8, 64);
        return true;
    }
};
struct Memory
{
    static inline bool fail = false;
    static inline unsigned releases = 0;
    static void* allocate(size_t size) { return fail ? nullptr : std::malloc(size); }
    static void release(void* memory)
    {
        const auto* bytes = static_cast<const uint8_t*>(memory);
        for (size_t index = 0; index < sizeof(Identity); ++index) assert(bytes[index] == 0);
        ++releases;
        std::free(memory);
    }
};
int main()
{
    std::array<uint8_t, 16> session{};
    std::array<uint8_t, 32> challenge{};
    std::array<uint8_t, 64> key{}, signature{};
    std::array<uint8_t, 160> work{};
    uint32_t generation = 0;
    {
        chat::StoredIdentityLease<Identity, Memory> lease;
        assert(lease.read(key.data(), generation) == chat::DeviceAuthorResult::Ok && generation == 7);
        assert(lease.prove(session.data(), challenge.data(), 7, work.data(), work.size(), key.data(), generation, signature.data()) == chat::DeviceAuthorResult::Ok);
        assert(lease.prove(session.data(), challenge.data(), 6, work.data(), work.size(), key.data(), generation, signature.data()) == chat::DeviceAuthorResult::StaleIdentity);
        assert(signature[0] == 0 && Identity::signs == 1);
        assert(lease.prove(session.data(), challenge.data(), 7, work.data(), 1, key.data(), generation, signature.data()) == chat::DeviceAuthorResult::InvalidInput);
    }
    assert(Memory::releases == 1);
    Identity::exists = false;
    {
        chat::StoredIdentityLease<Identity, Memory> lease;
        assert(lease.read(key.data(), generation) == chat::DeviceAuthorResult::Unavailable && generation == 0 && key[0] == 0);
    }
    Identity::bad = true;
    {
        chat::StoredIdentityLease<Identity, Memory> lease;
        assert(lease.result() == chat::DeviceAuthorResult::StorageError);
    }
    Memory::fail = true;
    const auto loads = Identity::loads;
    {
        chat::StoredIdentityLease<Identity, Memory> lease;
        assert(lease.result() == chat::DeviceAuthorResult::Unavailable);
    }
    assert(Identity::loads == loads && Memory::releases == 3);
    Memory::fail = false;
    Identity::exists = true;
    Identity::bad = false;
    {
        Identity::exists = false;
        unsigned initialized = 0;
        chat::StoredIdentityLease<Identity, Memory> account([&](Identity& value) {
            ++initialized;
            if (value.identityStorageError()) return false;
            Identity::exists = true;
            return value.initExisting();
        });
        assert(initialized == 1 && account.read(key.data(), generation) == chat::DeviceAuthorResult::Ok);
    }
    using Lease = chat::StoredIdentityLease<Identity, Memory>;
    for (const auto status : {chat::DeviceIdentityRestoreResult::Unavailable,
                              chat::DeviceIdentityRestoreResult::StaleIdentity,
                              chat::DeviceIdentityRestoreResult::InvalidInput,
                              chat::DeviceIdentityRestoreResult::StorageError,
                              chat::DeviceIdentityRestoreResult::Unsupported})
    {
        Lease::noteRestore(status);
        assert(!Lease::rebootRequired());
    }
    {
        Lease active;
        assert(active.get());
        Lease::noteRestore(chat::DeviceIdentityRestoreResult::CommittedRebootRequired);
        assert(Lease::rebootRequired() && !active.get());
        const auto before = Identity::loads;
        Lease later;
        assert(!later.get() && Identity::loads == before);
        assert(active.read(key.data(), generation) == chat::DeviceAuthorResult::Unavailable);
    }
    struct OtherMemory : Memory
    {
    };
    using OtherLease = chat::StoredIdentityLease<Identity, OtherMemory>;
    OtherLease::noteRestore(chat::DeviceIdentityRestoreResult::IndeterminateRebootRequired);
    assert(OtherLease::rebootRequired());
    const auto before = Identity::loads;
    OtherLease uncertain;
    assert(!uncertain.get() && Identity::loads == before);
}
