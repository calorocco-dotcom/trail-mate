#pragma once

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/storage/snapshot_stream_sha256.h"
#include "track_sharing/file_integrity.h"
#include "track_sharing/image_source_binding_store.h"
#include "track_sharing/installed_package_path.h"
#include <cstdio>
#include <new>
#include <string_view>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace platform::esp::route_images
{
namespace sd = platform::esp::arduino_common::storage;
// Hash-only owner avoids admitting any SD backing/path buffers for UI hashes.
class ImageReferenceHashWork
{
  public:
    ImageReferenceHashWork() : hash_(context_) {}
#if defined(ESP_PLATFORM)
    static void* operator new(size_t size, const std::nothrow_t&) noexcept
    {
        const auto caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
        return heap_caps_malloc(size, caps | MALLOC_CAP_8BIT);
    }
    static void operator delete(void* value) noexcept { heap_caps_free(value); }
#endif
    bool hash(std::string_view reference, uint8_t* output)
    {
        return output && !reference.empty() && reference.size() <= 4096 && hash_.start() && hash_.update(reinterpret_cast<const uint8_t*>(reference.data()), reference.size()) && hash_.finish(output);
    }

  private:
    mbedtls_sha256_context context_;
    platform::esp::storage::SnapshotStreamSha256 hash_;
};
static_assert(sizeof(ImageReferenceHashWork) <= 256, "Reference-only hashing must not include file buffers");
class SourceIo
{
  public:
    enum class Mode
    {
        Read,
        CreateTemporary
    };
    enum class OpenStatus
    {
        Ready,
        Missing,
        Unavailable
    };
    struct OpenResult
    {
        void* handle;
        OpenStatus status;
    };
    explicit SourceIo(sd::SdRuntimeMemoryPolicy policy = memoryPolicy()) : media_(sd::sd_media_session()), file_(policy) {}
    bool current() const { return file_.has_backing_storage() && media_ && media_ == sd::sd_media_session() && sd::sd_card_ready(); }
    OpenResult open(const char* path, Mode mode)
    {
        if (!current() || file_.is_open()) return {nullptr, OpenStatus::Unavailable};
        if (file_.open(path, mode == Mode::Read ? "r" : "w", media_)) return {this, OpenStatus::Ready};
        if (mode == Mode::Read)
        {
            // A bool open/exists failure cannot distinguish contention from
            // absence. A successful open needs no probe; after a failure only
            // an explicit Missing result is safe to treat as absence. The
            // semantic API reads whole files, so oversized/empty results are
            // NOT Missing and must not be interpreted as a prefix read.
            uint8_t first = 0;
            const auto probe = sd::sd_read_file(path, &first, 1);
            if (!current()) return {nullptr, OpenStatus::Unavailable};
            if (probe.status == sd::SdFileReadStatus::Missing) return {nullptr, OpenStatus::Missing};
        }
        return {nullptr, OpenStatus::Unavailable};
    }
    bool close(void*)
    {
        file_.close();
        return current();
    }
    bool size(void*, uint64_t& value)
    {
        if (!current()) return false;
        value = file_.size();
        return current() && !file_.read_busy();
    }
    bool seek(void*, uint64_t at) { return current() && file_.seek(at) && current(); }
    size_t read(void*, uint8_t* output, size_t length)
    {
        if (!current()) return 0;
        const auto count = file_.read(output, length);
        return current() && count >= 0 ? size_t(count) : 0;
    }
    size_t write(void*, const uint8_t* input, size_t length) { return current() ? file_.write(input, length) : 0; }
    bool sync(void*) { return current() && file_.flush() && current(); }
    bool remove(const char* path) { return current() && sd::sd_remove(path, media_); }
    bool rename(const char* from, const char* to) { return current() && sd::sd_rename(from, to, media_); }

  private:
    static sd::SdRuntimeMemoryPolicy memoryPolicy()
    {
#if defined(ESP_PLATFORM)
        if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) return sd::SdRuntimeMemoryPolicy::PsramOnly;
#endif
        return sd::SdRuntimeMemoryPolicy::Default;
    }
    uint32_t media_;
    sd::SdRuntimeFile file_;
};

// Short-lived owner, never an ESP automatic local. The SD driver backing,
// SHA context, verification scratch and path storage all belong to this owner.
// Caller holds the route-storage lease through download, verify and commit.
class RouteImageSourceWork
{
  public:
    RouteImageSourceWork() : hash_(context_), check_(io_, hash_), bindings_(io_) {}
    explicit RouteImageSourceWork(sd::SdRuntimeMemoryPolicy policy) : io_(policy), hash_(context_), check_(io_, hash_), bindings_(io_) {}
#if defined(ESP_PLATFORM)
    static void* operator new(size_t size, const std::nothrow_t&) noexcept
    {
        const auto memory = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
        return heap_caps_malloc(size, memory | MALLOC_CAP_8BIT);
    }
    static void operator delete(void* value) noexcept { heap_caps_free(value); }
#endif
    bool referenceHash(std::string_view source, uint8_t* output)
    {
        return !started_ && output && !source.empty() && source.size() <= 4096 && hash_.start() &&
               hash_.update(reinterpret_cast<const uint8_t*>(source.data()), source.size()) && hash_.finish(output);
    }
    bool begin(std::string_view source, std::string_view image)
    {
        if (started_ || !resolve(image) || !referenceHash(source, source_hash_) || !hash_.start()) return false;
        const auto opened = io_.open(candidate_, SourceIo::Mode::CreateTemporary);
        if (!opened.handle) return false;
        started_ = true;
        writing_ = true;
        return true;
    }
    // Offline callers already own the exact decoded reference digest. The
    // original signed package proof must be established independently.
    bool beginHashed(std::string_view image, const uint8_t source_hash[32])
    {
        if (started_ || !source_hash || !resolve(image) || !hash_.start()) return false;
        std::memcpy(source_hash_, source_hash, 32);
        const auto opened = io_.open(candidate_, SourceIo::Mode::CreateTemporary);
        if (!opened.handle) return false;
        started_ = writing_ = true;
        return true;
    }
    bool feed(const uint8_t* input, size_t length)
    {
        if (!writing_ || failed_ || (!input && length) || length > track_sharing::kImageSourceByteLimit - bytes_) return failed_ = true, false;
        if (!hash_.update(input, length) || io_.write(&io_, input, length) != length) return failed_ = true, false;
        bytes_ += uint32_t(length);
        return true;
    }
    bool seal()
    {
        if (!writing_) return false;
        const bool flushed = io_.sync(&io_), closed = io_.close(&io_);
        writing_ = false;
        if (failed_ || !bytes_ || !flushed || !closed || !hash_.finish(file_hash_)) return false;
        return check_.begin(candidate_, bytes_, file_hash_) == track_sharing::FileIntegrityResult::Pending;
    }
    track_sharing::FileIntegrityResult verifyStep() { return check_.step(scratch_, sizeof(scratch_)); }
    bool verifying() const { return check_.active(); }
    uint32_t bytes() const { return bytes_; }
    bool contentMatches(uint32_t bytes, const uint8_t hash[32]) const
    {
        return hash && !writing_ && !failed_ && bytes_ == bytes && !std::memcmp(file_hash_, hash, 32);
    }
    bool commit()
    {
        const track_sharing::ImageSourceBindingView expected = {{source_hash_, 32}, {file_hash_, 32}, bytes_};
        const track_sharing::ImageBindingPaths paths = {candidate_, image_, candidate_binding_, binding_, preview_, view_};
        return bindings_.commit(paths, expected, check_, scratch_, sizeof(scratch_)) == track_sharing::ImageBindingResult::Ready;
    }
    // Fast provenance/length query, not an independent full-file hash proof.
    // Signed attachment admission must also verify the actual content digest.
    bool matchesExisting(std::string_view image, const uint8_t* source_hash)
    {
        if (started_ || !source_hash || !resolve(image)) return false;
        track_sharing::ImageSourceBindingView record;
        if (bindings_.read(binding_, scratch_, sizeof(scratch_), record) != track_sharing::ImageBindingResult::Ready ||
            std::memcmp(record.source_hash.data, source_hash, 32)) return false;
        const auto file = io_.open(image_, SourceIo::Mode::Read);
        if (!file.handle) return false;
        uint64_t size = 0;
        const bool sized = io_.size(file.handle, size), closed = io_.close(file.handle);
        return sized && closed && size == record.file_bytes;
    }
    bool matchesExistingReference(std::string_view image, std::string_view source)
    {
        return referenceHash(source, source_hash_) && matchesExisting(image, source_hash_);
    }

  private:
    bool resolve(std::string_view image)
    {
        constexpr std::string_view root = "/routes/.trailmate/";
        if (image.substr(0, root.size()) != root) return false;
        const auto separator = image.find('/', root.size());
        if (separator == image.npos) return false;
        const auto id = image.substr(root.size(), separator - root.size());
        if (id.empty() || (id.size() > 48 && !track_sharing::isInstalledPackageAssetId(id)) || id == "." || id == "..") return false;
        for (const char c : id)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
        const auto suffix = image.substr(separator);
        if (suffix.size() != 20 || suffix.substr(0, 12) != "/images/img-" || suffix.substr(16) != ".jpg") return false;
        unsigned index = 0;
        for (size_t i = 12; i < 16; ++i)
        {
            if (suffix[i] < '0' || suffix[i] > '9') return false;
            index = index * 10 + unsigned(suffix[i] - '0');
        }
        if (!index || index > 64 || image.size() + 12 >= sizeof(image_)) return false;
        std::memcpy(image_, image.data(), image.size());
        image_[image.size()] = '\0';
        const auto set = [&](char* output, const char* format)
        { return std::snprintf(output, sizeof(image_), format, image_) > 0; };
        if (!set(candidate_, "%s.tmp") || !set(binding_, "%s.source") || !set(candidate_binding_, "%s.source.new")) return false;
        const auto base = image.substr(0, separator);
        return std::snprintf(preview_, sizeof(preview_), "%.*s/thumbs/thumb-%04u.bmp", int(base.size()), base.data(), index) > 0 &&
               std::snprintf(view_, sizeof(view_), "%.*s/views/view-%04u.bmp", int(base.size()), base.data(), index) > 0;
    }
    SourceIo io_;
    mbedtls_sha256_context context_;
    platform::esp::storage::SnapshotStreamSha256 hash_;
    track_sharing::FileIntegrityCheck<SourceIo, decltype(hash_)> check_;
    track_sharing::ImageSourceBindingStore<SourceIo> bindings_;
    uint8_t source_hash_[32]{}, file_hash_[32]{}, scratch_[320]{};
    char candidate_[128]{}, image_[128]{}, candidate_binding_[128]{}, binding_[128]{}, preview_[128]{}, view_[128]{};
    uint32_t bytes_ = 0;
    bool started_ = false, writing_ = false, failed_ = false;
};
} // namespace platform::esp::route_images
