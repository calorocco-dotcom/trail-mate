#pragma once
#include "track_sharing/image_source_binding.h"

namespace track_sharing
{
enum class ImageBindingResult : uint8_t
{
    Ready,
    Missing,
    Corrupt,
    Invalid,
    Unverified,
    StorageFailure
};
struct ImageBindingPaths
{
    const char* candidate_image;
    const char* image;
    const char* candidate_binding;
    const char* binding;
    const char* preview;
    const char* view;
};
// No allocation or retained buffers. The caller supplies exclusive, canonical,
// distinct device-derived paths, a media lease and one 75-byte scratch buffer.
// Source/file hashes borrow separate immutable storage. Legacy unbound images
// are Missing, never silently promoted to authenticated or source-bound data.
template <class Io>
class ImageSourceBindingStore
{
  public:
    explicit ImageSourceBindingStore(Io& io) : io_(io) {}
    ImageBindingResult read(const char* path, uint8_t* scratch, size_t capacity, ImageSourceBindingView& output)
    {
        if (!path || !*path || !scratch || capacity < kImageSourceBindingBytes ||
            !imageBindingDisjoint(scratch, capacity, this, sizeof(*this)) ||
            !imageBindingDisjoint(scratch, capacity, path, std::strlen(path) + 1) ||
            !imageBindingDisjoint(scratch, capacity, &output, sizeof(output))) return ImageBindingResult::Invalid;
        output = {};
        const auto file = io_.open(path, Io::Mode::Read);
        if (!file.handle) return file.status == Io::OpenStatus::Missing ? ImageBindingResult::Missing : ImageBindingResult::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(file.handle, size);
        const bool loaded = sized && size == kImageSourceBindingBytes && io_.seek(file.handle, 0) &&
                            io_.read(file.handle, scratch, kImageSourceBindingBytes) == kImageSourceBindingBytes;
        const bool closed = io_.close(file.handle);
        if (!sized || !closed) return ImageBindingResult::StorageFailure;
        if (size != kImageSourceBindingBytes) return ImageBindingResult::Corrupt;
        if (!loaded) return ImageBindingResult::StorageFailure;
        return decodeImageSourceBinding(scratch, kImageSourceBindingBytes, output) ? ImageBindingResult::Ready : ImageBindingResult::Corrupt;
    }
    template <class Verification>
    ImageBindingResult commit(const ImageBindingPaths& paths, const ImageSourceBindingView& expected,
                              const Verification& verified, uint8_t* scratch, size_t capacity)
    {
        if (!scratch || !imageBindingDisjoint(scratch, capacity, this, sizeof(*this)) ||
            !imageBindingDisjoint(scratch, capacity, &paths, sizeof(paths)) ||
            !imageBindingDisjoint(scratch, capacity, &verified, sizeof(verified)) ||
            !validPaths(paths, scratch, capacity) || !encodeImageSourceBinding(expected, scratch, capacity)) return ImageBindingResult::Invalid;
        if (!verified.matchesSource(paths.candidate_image, expected.file_bytes, expected.file_hash.data)) return ImageBindingResult::Unverified;
        const auto candidate = io_.open(paths.candidate_image, Io::Mode::Read);
        if (!candidate.handle) return candidate.status == Io::OpenStatus::Missing ? ImageBindingResult::Unverified : ImageBindingResult::StorageFailure;
        uint64_t image_bytes = 0;
        const bool sized = io_.size(candidate.handle, image_bytes), candidate_closed = io_.close(candidate.handle);
        if (!sized || !candidate_closed) return ImageBindingResult::StorageFailure;
        if (image_bytes != expected.file_bytes) return ImageBindingResult::Unverified;
        const auto file = io_.open(paths.candidate_binding, Io::Mode::CreateTemporary);
        if (!file.handle) return ImageBindingResult::StorageFailure;
        const bool written = io_.write(file.handle, scratch, kImageSourceBindingBytes) == kImageSourceBindingBytes && io_.sync(file.handle);
        const bool closed = io_.close(file.handle);
        if (!written || !closed) return ImageBindingResult::StorageFailure;
        ImageSourceBindingView check;
        if (read(paths.candidate_binding, scratch, capacity, check) != ImageBindingResult::Ready || !imageSourceBindingMatches(check, expected)) return ImageBindingResult::StorageFailure;
        // Invalidate provenance before changing the published image or derived
        // bitmaps. Any later failure leaves the new bytes explicitly unbound.
        if (!removePresent(paths.binding) || !removePresent(paths.preview) || !removePresent(paths.view) ||
            !removePresent(paths.image) || !io_.rename(paths.candidate_image, paths.image) ||
            !io_.rename(paths.candidate_binding, paths.binding)) return ImageBindingResult::StorageFailure;
        if (read(paths.binding, scratch, capacity, check) != ImageBindingResult::Ready || !imageSourceBindingMatches(check, expected)) return ImageBindingResult::StorageFailure;
        return ImageBindingResult::Ready;
    }

  private:
    static bool validPaths(const ImageBindingPaths& paths, uint8_t* scratch, size_t capacity)
    {
        const char* values[] = {paths.candidate_image, paths.image, paths.candidate_binding, paths.binding, paths.preview, paths.view};
        for (size_t i = 0; i < 6; ++i)
        {
            if (!values[i] || !*values[i] || !imageBindingDisjoint(scratch, capacity, values[i], std::strlen(values[i]) + 1)) return false;
            for (size_t j = 0; j < i; ++j)
            {
                size_t k = 0;
                const auto lower = [](char c)
                { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
                while (values[i][k] && lower(values[i][k]) == lower(values[j][k])) ++k;
                if (!values[i][k] && !values[j][k]) return false;
            }
        }
        return true;
    }
    bool removePresent(const char* path)
    {
        const auto file = io_.open(path, Io::Mode::Read);
        if (!file.handle) return file.status == Io::OpenStatus::Missing;
        return io_.close(file.handle) && io_.remove(path);
    }
    Io& io_;
};
} // namespace track_sharing
