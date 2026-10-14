#pragma once

#include "platform/esp/common/mbedtls_sha256_compat.h"
#include "platform/esp/common/storage/track_snapshot_file_storage.h"

namespace platform::esp::storage
{
// Caller lends an exclusive, uninitialized context for this object's lifetime.
// No context copy/allocation or internal-RAM fallback occurs here. The owner
// must include the real context size in its short-lived resource admission.
class SnapshotStreamSha256 final : public SnapshotStreamHash
{
  public:
    explicit SnapshotStreamSha256(mbedtls_sha256_context& context) : context_(context) { mbedtls_sha256_init(&context_); }
    ~SnapshotStreamSha256() override { mbedtls_sha256_free(&context_); }
    SnapshotStreamSha256(const SnapshotStreamSha256&) = delete;
    SnapshotStreamSha256& operator=(const SnapshotStreamSha256&) = delete;
    bool start() override
    {
        reset();
        active_ = platform::esp::common::crypto::sha256_starts(&context_, 0) == 0;
        return active_;
    }
    bool update(const uint8_t* bytes, size_t length) override
    {
        if (!active_ || (!bytes && length))
        {
            reset();
            return false;
        }
        if (platform::esp::common::crypto::sha256_update(&context_, bytes, length) != 0)
        {
            reset();
            return false;
        }
        return true;
    }
    bool finish(uint8_t output[32]) override
    {
        if (!active_ || !output)
        {
            reset();
            return false;
        }
        const bool result = platform::esp::common::crypto::sha256_finish(&context_, output) == 0;
        reset();
        return result;
    }
    void reset() override
    {
        mbedtls_sha256_free(&context_);
        mbedtls_sha256_init(&context_);
        active_ = false;
    }

  private:
    mbedtls_sha256_context& context_;
    bool active_ = false;
};
} // namespace platform::esp::storage
