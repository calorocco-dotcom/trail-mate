#pragma once
#include "geocaching/domain/record.h"
#include <algorithm>
#include <cstring>
namespace track_sharing
{
enum class FileCopyResult : uint8_t
{
    Pending,
    Copied,
    StorageFailure,
    Conflict,
    Invalid
};
// Caller admits an exclusive, media-bound temporary file and retains offset.
// This primitive does not authorize paths or declare content hash verification.
template <class Io>
FileCopyResult copyFileSlice(Io& io, const char* source, const char* temporary, uint32_t bytes,
                             uint32_t offset, uint8_t* scratch, size_t capacity, uint32_t& next)
{
    if (!source || !*source || !temporary || !*temporary || !std::strcmp(source, temporary) ||
        !bytes || bytes > 8388608 || offset >= bytes || !scratch || !capacity) return FileCopyResult::Invalid;
    const size_t count = std::min({size_t(320), capacity, size_t(bytes - offset)});
    const auto overlaps = [](const void* a, size_t na, const void* b, size_t nb)
    {
        const auto x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
        return na && nb && (x <= y ? y - x < na : x - y < nb);
    };
    const size_t source_length = std::strlen(source) + 1, temporary_length = std::strlen(temporary) + 1;
    if (overlaps(scratch, count, source, source_length) || overlaps(scratch, count, temporary, temporary_length) ||
        overlaps(scratch, count, &next, sizeof(next)) || overlaps(scratch, count, &io, sizeof(io)) ||
        overlaps(&next, sizeof(next), source, source_length) || overlaps(&next, sizeof(next), temporary, temporary_length) ||
        overlaps(&next, sizeof(next), &io, sizeof(io))) return FileCopyResult::Invalid;
    next = offset;
    const auto input = io.open(source, Io::Mode::Read);
    if (!input.handle) return FileCopyResult::StorageFailure;
    uint64_t source_size = 0;
    const bool sized = io.size(input.handle, source_size);
    const bool read = sized && source_size == bytes && io.seek(input.handle, offset) && io.read(input.handle, scratch, count) == count;
    const bool closed = io.close(input.handle);
    if (!sized || !closed) return FileCopyResult::StorageFailure;
    if (source_size != bytes) return FileCopyResult::Conflict;
    if (!read) return FileCopyResult::StorageFailure;
    const auto output = io.open(temporary, Io::Mode::Update);
    if (!output.handle) return FileCopyResult::StorageFailure;
    uint64_t destination_size = 0;
    const bool known = io.size(output.handle, destination_size);
    // A short failed write is never silently treated as a durable offset.
    const bool written = known && destination_size == offset && io.seek(output.handle, offset) &&
                         io.write(output.handle, scratch, count) == count && io.sync(output.handle);
    const bool output_closed = io.close(output.handle);
    if (!known || !output_closed) return FileCopyResult::StorageFailure;
    if (destination_size != offset) return FileCopyResult::Conflict;
    if (!written) return FileCopyResult::StorageFailure;
    next = offset + uint32_t(count);
    return next == bytes ? FileCopyResult::Copied : FileCopyResult::Pending;
}
} // namespace track_sharing
