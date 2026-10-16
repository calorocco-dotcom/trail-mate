#pragma once
#include "track_sharing/sign_manifest.h"
#include <algorithm>
namespace track_sharing
{
enum class SignedStoreResult : uint8_t
{
    Ready,
    Missing,
    Conflict,
    StorageFailure,
    Invalid
};
// Byte persistence only: caller has authorized/signature-verified immutable
// inputs, exclusive canonical paths and media-bound I/O. No owned byte buffers.
template <class Io>
class SignedManifestStore
{
  public:
    using Result = SignedStoreResult;
    SignedManifestStore(Io& io, const char* temporary, const char* destination) : io_(io), temporary_(temporary), destination_(destination) {}
    Result persist(geocaching::ByteView raw, geocaching::ByteView signature, uint8_t* scratch, size_t capacity)
    {
        if (!raw.data || !raw.size || raw.size > 4096 || !signature.data || signature.size != 64 || !scratch || capacity < 128 || !temporary_ || !destination_ || !*temporary_ || !*destination_ || !std::strcmp(temporary_, destination_) ||
            signingSpansOverlap(scratch, 128, raw.data, raw.size) || signingSpansOverlap(scratch, 128, signature.data, signature.size)) return Result::Invalid;
        const auto final = compare(destination_, raw, signature, scratch);
        if (final != Result::Missing) return final;
        const auto existing = compare(temporary_, raw, signature, scratch);
        if (existing != Result::Missing && existing != Result::Ready) return existing;
        if (existing == Result::Missing)
        {
            const auto opened = io_.open(temporary_, Io::Mode::CreateTemporary);
            if (!opened.handle) return Result::StorageFailure;
            const size_t prefix = raw.size <= 255 ? 3 : 4;
            scratch[0] = 0x92;
            scratch[1] = raw.size <= 255 ? 0xc4 : 0xc5;
            if (prefix == 3) scratch[2] = uint8_t(raw.size);
            else
            {
                scratch[2] = uint8_t(raw.size >> 8);
                scratch[3] = uint8_t(raw.size);
            }
            bool ok = io_.write(opened.handle, scratch, prefix) == prefix;
            for (size_t offset = 0; ok && offset < raw.size;)
            {
                const size_t count = std::min(size_t(320), raw.size - offset);
                ok = io_.write(opened.handle, raw.data + offset, count) == count;
                offset += count;
            }
            scratch[0] = 0xc4;
            scratch[1] = 64;
            ok = ok && io_.write(opened.handle, scratch, 2) == 2 && io_.write(opened.handle, signature.data, 64) == 64 && io_.sync(opened.handle);
            const bool closed = io_.close(opened.handle);
            if (!ok || !closed) return Result::StorageFailure;
            if (compare(temporary_, raw, signature, scratch) != Result::Ready) return Result::StorageFailure;
        }
        if (!io_.publish(temporary_, destination_)) return Result::StorageFailure;
        return compare(destination_, raw, signature, scratch) == Result::Ready ? Result::Ready : Result::StorageFailure;
    }

  private:
    Result compare(const char* path, geocaching::ByteView raw, geocaching::ByteView signature, uint8_t* scratch)
    {
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? Result::Missing : Result::StorageFailure;
        const size_t prefix = raw.size <= 255 ? 3 : 4;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size);
        bool io_ok = sized && io_.seek(opened.handle, 0), equal = sized && size == raw.size + prefix + 66;
        if (io_ok && equal)
        {
            io_ok = io_.read(opened.handle, scratch, prefix) == prefix;
            equal = io_ok && scratch[0] == 0x92 && scratch[1] == (prefix == 3 ? 0xc4 : 0xc5) && (prefix == 3 ? scratch[2] == raw.size : ((size_t(scratch[2]) << 8) | scratch[3]) == raw.size);
        }
        for (size_t offset = 0; io_ok && equal && offset < raw.size;)
        {
            const size_t count = std::min(size_t(128), raw.size - offset);
            io_ok = io_.read(opened.handle, scratch, count) == count;
            equal = io_ok && !std::memcmp(scratch, raw.data + offset, count);
            offset += count;
        }
        if (io_ok && equal)
        {
            io_ok = io_.read(opened.handle, scratch, 2) == 2;
            equal = io_ok && scratch[0] == 0xc4 && scratch[1] == 64;
        }
        if (io_ok && equal)
        {
            io_ok = io_.read(opened.handle, scratch, 64) == 64;
            equal = io_ok && !std::memcmp(scratch, signature.data, 64);
        }
        const bool closed = io_.close(opened.handle);
        return !io_ok || !closed ? Result::StorageFailure : equal ? Result::Ready
                                                                  : Result::Conflict;
    }
    Io& io_;
    const char* temporary_;
    const char* destination_;
};
} // namespace track_sharing
