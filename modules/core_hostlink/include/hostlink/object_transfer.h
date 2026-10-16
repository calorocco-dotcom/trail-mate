#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hostlink::management
{
enum class TransferResult : uint8_t
{
    Ok,
    Busy,
    Invalid,
    WrongOwner,
    Conflict,
    StorageFailure,
    HashMismatch,
    NotFound,
};

struct TransferDescriptor
{
    uint8_t owner[16]{};
    uint8_t id[16]{};
    uint8_t sha256[32]{};
    uint32_t bytes = 0;
    uint16_t kind = 0;
};

// Platform storage owns its file/hash context. No whole-file buffer is allowed.
// Each append must be durable before returning true. On failure it must truncate
// back to the previous durable offset so a retry cannot append the bytes twice.
class TransferStorage
{
  public:
    virtual ~TransferStorage() = default;
    virtual bool begin(const TransferDescriptor& descriptor) = 0;
    virtual bool append(uint32_t offset, const uint8_t* bytes, size_t length) = 0;
    virtual bool matches(uint32_t offset, const uint8_t* bytes, size_t length) = 0;
    virtual bool verify(const uint8_t expected_sha256[32], uint32_t length) = 0;
    virtual bool seal() = 0;
    virtual bool discard() = 0;
};

// Single-slot coordinator: metadata only, no payload buffers, queues or tasks.
// Owner is the authorised device session, not a USB device/vendor ID. The
// platform may restore a descriptor/offset from its validated storage journal.
class ObjectTransfer
{
  public:
    static constexpr size_t kChunkBytes = 384;
    explicit ObjectTransfer(TransferStorage& storage) : storage_(storage) {}
    uint32_t offset() const { return offset_; }
    bool active() const { return active_; }
    bool sealed() const { return sealed_; }
    const TransferDescriptor& descriptor() const { return descriptor_; }

    TransferResult begin(const TransferDescriptor& descriptor, uint32_t limit)
    {
        if (!descriptor.bytes || descriptor.bytes > limit) return TransferResult::Invalid;
        if (active_)
        {
            if (std::memcmp(descriptor.owner, descriptor_.owner, 16)) return TransferResult::WrongOwner;
            return same(descriptor) ? TransferResult::Ok : TransferResult::Busy;
        }
        if (!storage_.begin(descriptor)) return TransferResult::StorageFailure;
        descriptor_ = descriptor;
        offset_ = 0;
        active_ = true;
        sealed_ = false;
        return TransferResult::Ok;
    }
    TransferResult append(const uint8_t owner[16], const uint8_t id[16], uint32_t offset,
                          const uint8_t* bytes, size_t length)
    {
        const auto auth = check(owner, id);
        if (auth != TransferResult::Ok) return auth;
        if (!bytes || !length || length > kChunkBytes || offset > descriptor_.bytes ||
            length > descriptor_.bytes - offset) return TransferResult::Invalid;
        if (offset < offset_)
        {
            if (length > offset_ - offset) return TransferResult::Conflict;
            return storage_.matches(offset, bytes, length) ? TransferResult::Ok : TransferResult::Conflict;
        }
        if (sealed_ || offset != offset_) return TransferResult::Conflict;
        if (!storage_.append(offset, bytes, length)) return TransferResult::StorageFailure;
        offset_ += static_cast<uint32_t>(length);
        return TransferResult::Ok;
    }
    TransferResult finish(const uint8_t owner[16], const uint8_t id[16])
    {
        const auto auth = check(owner, id);
        if (auth != TransferResult::Ok) return auth;
        if (sealed_) return TransferResult::Ok;
        if (offset_ != descriptor_.bytes) return TransferResult::Conflict;
        if (!storage_.verify(descriptor_.sha256, descriptor_.bytes)) return TransferResult::HashMismatch;
        if (!storage_.seal()) return TransferResult::StorageFailure;
        sealed_ = true;
        return TransferResult::Ok;
    }
    TransferResult abort(const uint8_t owner[16], const uint8_t id[16])
    {
        const auto auth = check(owner, id);
        if (auth != TransferResult::Ok) return auth;
        if (!storage_.discard()) return TransferResult::StorageFailure;
        active_ = sealed_ = false;
        offset_ = 0;
        descriptor_ = {};
        return TransferResult::Ok;
    }
    // Called only after the platform authenticates and validates its durable
    // journal/file. A remote client cannot assert a restored offset itself.
    bool restore(const TransferDescriptor& descriptor, uint32_t durable_offset, bool sealed)
    {
        if (active_ || !descriptor.bytes || durable_offset > descriptor.bytes ||
            (sealed && durable_offset != descriptor.bytes)) return false;
        descriptor_ = descriptor;
        offset_ = durable_offset;
        active_ = true;
        sealed_ = sealed;
        return true;
    }

  private:
    bool same(const TransferDescriptor& other) const
    {
        return !std::memcmp(other.id, descriptor_.id, 16) &&
               !std::memcmp(other.sha256, descriptor_.sha256, 32) &&
               other.bytes == descriptor_.bytes && other.kind == descriptor_.kind;
    }
    TransferResult check(const uint8_t* owner, const uint8_t* id) const
    {
        if (!active_) return TransferResult::NotFound;
        if (!owner || std::memcmp(owner, descriptor_.owner, 16)) return TransferResult::WrongOwner;
        if (!id || std::memcmp(id, descriptor_.id, 16)) return TransferResult::NotFound;
        return TransferResult::Ok;
    }
    TransferStorage& storage_;
    TransferDescriptor descriptor_{};
    uint32_t offset_ = 0;
    bool active_ = false, sealed_ = false;
};
static_assert(sizeof(ObjectTransfer) <= 96, "Transfer metadata must not grow into payload storage");
} // namespace hostlink::management
