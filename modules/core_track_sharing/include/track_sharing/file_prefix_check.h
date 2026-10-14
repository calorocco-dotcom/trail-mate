#pragma once
#include "track_sharing/file_copy_slice.h"
#include "track_sharing/sign_manifest.h"
namespace track_sharing
{
// Checks an existing temporary prefix before resuming. The final checked slice
// syncs the prefix; the complete file must still pass SHA validation afterward.
template <class Io>
FileCopyResult checkFilePrefixSlice(Io& io, const char* source, const char* temporary, uint32_t bytes,
                                    uint32_t prefix, uint32_t checked, uint8_t* scratch, size_t capacity, uint32_t& next)
{
    if (!source || !temporary || !*source || !*temporary || !std::strcmp(source, temporary) || !bytes || bytes > 8388608 ||
        !prefix || prefix >= bytes || checked >= prefix || !scratch || capacity < 2) return FileCopyResult::Invalid;
    const size_t count = std::min({size_t(160), capacity / 2, size_t(prefix - checked)});
    if (signingSpansOverlap(scratch, count * 2, reinterpret_cast<const uint8_t*>(source), std::strlen(source) + 1) ||
        signingSpansOverlap(scratch, count * 2, reinterpret_cast<const uint8_t*>(temporary), std::strlen(temporary) + 1) ||
        signingSpansOverlap(scratch, count * 2, reinterpret_cast<const uint8_t*>(&next), sizeof(next)) ||
        signingSpansOverlap(scratch, count * 2, reinterpret_cast<const uint8_t*>(&io), sizeof(io)) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&next), sizeof(next), reinterpret_cast<const uint8_t*>(source), std::strlen(source) + 1) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&next), sizeof(next), reinterpret_cast<const uint8_t*>(temporary), std::strlen(temporary) + 1) ||
        signingSpansOverlap(reinterpret_cast<const uint8_t*>(&next), sizeof(next), reinterpret_cast<const uint8_t*>(&io), sizeof(io))) return FileCopyResult::Invalid;
    next = checked;
    const auto input = io.open(source, Io::Mode::Read);
    if (!input.handle) return FileCopyResult::StorageFailure;
    uint64_t source_size = 0;
    const bool source_known = io.size(input.handle, source_size);
    const bool read = source_known && source_size == bytes && io.seek(input.handle, checked) && io.read(input.handle, scratch, count) == count;
    const bool closed = io.close(input.handle);
    if (!source_known || !closed) return FileCopyResult::StorageFailure;
    if (source_size != bytes) return FileCopyResult::Conflict;
    if (!read) return FileCopyResult::StorageFailure;
    const auto output = io.open(temporary, Io::Mode::Update);
    if (!output.handle) return FileCopyResult::StorageFailure;
    uint64_t temporary_size = 0;
    const bool known = io.size(output.handle, temporary_size);
    const bool read_prefix = known && temporary_size == prefix && io.seek(output.handle, checked) && io.read(output.handle, scratch + count, count) == count;
    const bool matches = read_prefix && !std::memcmp(scratch, scratch + count, count);
    const bool synced = !matches || checked + count != prefix || io.sync(output.handle);
    const bool output_closed = io.close(output.handle);
    if (!known || !output_closed || !synced) return FileCopyResult::StorageFailure;
    if (temporary_size != prefix) return FileCopyResult::Conflict;
    if (!read_prefix) return FileCopyResult::StorageFailure;
    if (!matches) return FileCopyResult::Conflict;
    next = checked + uint32_t(count);
    return next == prefix ? FileCopyResult::Copied : FileCopyResult::Pending;
}
} // namespace track_sharing
