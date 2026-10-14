#pragma once
#include "track_sharing/network_request.h"
#include <algorithm>

namespace track_sharing
{
enum class ReadStoreResult : uint8_t
{
    Ready,
    Missing,
    Conflict,
    StorageFailure,
    Invalid
};

// Byte persistence only. Caller validates the full response, owns exclusive
// paths and pins media/account/session leases. The temporary path is disposable
// for this one operation; a published response is never overwritten. Caller
// retains the original response digest for whole-response verification on USB.
template <class Io>
class ReadResponseStore
{
  public:
    using Result = ReadStoreResult;
    ReadResponseStore(Io& io, const char* temporary, const char* destination)
        : io_(io), temporary_(temporary), destination_(destination) {}
    Result persist(geocaching::ByteView source, uint8_t* scratch, size_t capacity)
    {
        if (!source.data || !source.size || source.size > 8192 || !scratch || capacity < 128 ||
            !temporary_ || !destination_ || !*temporary_ || !*destination_ || !std::strcmp(temporary_, destination_) ||
            !networkInputDisjoint(source, scratch, 128) ||
            !networkInputDisjoint(source, reinterpret_cast<uint8_t*>(this), sizeof(*this)) ||
            !networkInputDisjoint({scratch, 128}, reinterpret_cast<uint8_t*>(this), sizeof(*this))) return Result::Invalid;
        const auto published = compare(destination_, source, scratch);
        if (published != Result::Missing) return published;
        const auto staged = compare(temporary_, source, scratch);
        if (staged == Result::StorageFailure) return staged;
        if (staged != Result::Ready)
        {
            const auto opened = io_.open(temporary_, Io::Mode::CreateTemporary);
            if (!opened.handle) return Result::StorageFailure;
            bool ok = true;
            for (size_t offset = 0; ok && offset < source.size;)
            {
                const size_t count = std::min(size_t(320), source.size - offset);
                ok = io_.write(opened.handle, source.data + offset, count) == count;
                offset += count;
            }
            ok = ok && io_.sync(opened.handle);
            const bool closed = io_.close(opened.handle);
            if (!ok || !closed || compare(temporary_, source, scratch) != Result::Ready) return Result::StorageFailure;
        }
        else
        {
            // An earlier write may have completed while its sync failed.
            // Matching bytes alone are not evidence of durable staging.
            const auto opened = io_.open(temporary_, Io::Mode::Update);
            if (!opened.handle) return Result::StorageFailure;
            const bool synced = io_.sync(opened.handle);
            const bool closed = io_.close(opened.handle);
            if (!synced || !closed) return Result::StorageFailure;
        }
        if (!io_.publish(temporary_, destination_)) return Result::StorageFailure;
        return compare(destination_, source, scratch) == Result::Ready ? Result::Ready : Result::StorageFailure;
    }
    Result read(uint32_t total, uint32_t offset, uint16_t count, uint8_t* output, size_t capacity, size_t& written)
    {
        written = 0;
        if (!destination_ || !*destination_ || !total || total > 8192 || offset >= total || !count || count > 320 || count > total - offset ||
            !output || capacity < count || !networkInputDisjoint({output, count}, reinterpret_cast<uint8_t*>(this), sizeof(*this))) return Result::Invalid;
        const auto opened = io_.open(destination_, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? Result::Missing : Result::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size);
        const bool match = sized && size == total;
        const bool ok = match && io_.seek(opened.handle, offset) && io_.read(opened.handle, output, count) == count;
        const bool closed = io_.close(opened.handle);
        if (!sized || !closed) return Result::StorageFailure;
        if (!match) return Result::Conflict;
        if (!ok) return Result::StorageFailure;
        written = count;
        return Result::Ready;
    }

  private:
    Result compare(const char* path, geocaching::ByteView source, uint8_t* scratch)
    {
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? Result::Missing : Result::StorageFailure;
        uint64_t size = 0;
        bool ok = io_.size(opened.handle, size);
        bool match = ok && size == source.size;
        if (ok && match) ok = io_.seek(opened.handle, 0);
        for (size_t offset = 0; ok && match && offset < source.size;)
        {
            const size_t count = std::min(size_t(128), source.size - offset);
            ok = io_.read(opened.handle, scratch, count) == count;
            match = ok && !std::memcmp(scratch, source.data + offset, count);
            offset += count;
        }
        const bool closed = io_.close(opened.handle);
        if (!ok || !closed) return Result::StorageFailure;
        return match ? Result::Ready : Result::Conflict;
    }
    Io& io_;
    const char* temporary_;
    const char* destination_;
};
} // namespace track_sharing
