#pragma once
#include "track_sharing/publication_directory.h"
#include "track_sharing/publication_journal_store.h"

namespace track_sharing
{
// Immutable per-job sidecar. Caller supplies canonical path, exclusive SD lease,
// authenticated desired binding and caller-owned PSRAM records/scratch. Never
// replace an existing pin, including a damaged or partially written one.
template <class Io>
class PublicationDirectoryStore
{
  public:
    PublicationDirectoryStore(Io& io, const char* path) : io_(io), path_(path) {}
    PublicationStoreResult load(PublicationDirectory& output, uint8_t* scratch, size_t capacity)
    {
        if (!valid(output, scratch, capacity)) return PublicationStoreResult::Invalid;
        output = {};
        const auto opened = io_.open(path_, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? PublicationStoreResult::Missing : PublicationStoreResult::StorageFailure;
        uint64_t size = 0;
        const bool sized = io_.size(opened.handle, size);
        const bool read = sized && size == kPublicationDirectoryBytes && io_.read(opened.handle, scratch, kPublicationDirectoryBytes) == kPublicationDirectoryBytes;
        const bool closed = io_.close(opened.handle);
        if (!sized || !closed || (size == kPublicationDirectoryBytes && !read)) return PublicationStoreResult::StorageFailure;
        return read && decodePublicationDirectory(scratch, kPublicationDirectoryBytes, output) ? PublicationStoreResult::Ready : PublicationStoreResult::Corrupt;
    }
    PublicationStoreResult admit(const PublicationDirectory& desired, PublicationDirectory& verify, uint8_t* scratch, size_t capacity)
    {
        if (signingSpansOverlap(reinterpret_cast<const uint8_t*>(&desired), sizeof(desired), reinterpret_cast<const uint8_t*>(&verify), sizeof(verify)) ||
            !valid(verify, scratch, capacity) || !publicationDirectoryValid(desired) ||
            signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(&desired), sizeof(desired))) return PublicationStoreResult::Invalid;
        const auto existing = load(verify, scratch, capacity);
        if (existing == PublicationStoreResult::Ready) return publicationDirectoriesEqual(desired, verify) ? existing : PublicationStoreResult::Conflict;
        if (existing != PublicationStoreResult::Missing) return existing;
        if (!encodePublicationDirectory(desired, scratch, capacity)) return PublicationStoreResult::Invalid;
        const auto opened = io_.open(path_, Io::Mode::CreateTemporary);
        if (!opened.handle) return PublicationStoreResult::StorageFailure;
        const bool written = io_.write(opened.handle, scratch, kPublicationDirectoryBytes) == kPublicationDirectoryBytes && io_.sync(opened.handle);
        const bool closed = io_.close(opened.handle);
        if (!written || !closed) return PublicationStoreResult::StorageFailure;
        const auto checked = load(verify, scratch, capacity);
        if (checked != PublicationStoreResult::Ready) return checked;
        return publicationDirectoriesEqual(desired, verify) ? PublicationStoreResult::Ready : PublicationStoreResult::Conflict;
    }

  private:
    bool valid(const PublicationDirectory& record, uint8_t* scratch, size_t capacity) const
    {
        return path_ && *path_ && scratch && capacity >= kPublicationDirectoryBytes &&
               !signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(&record), sizeof(record)) &&
               !signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(path_), std::strlen(path_) + 1) &&
               !signingSpansOverlap(reinterpret_cast<const uint8_t*>(&record), sizeof(record), reinterpret_cast<const uint8_t*>(path_), std::strlen(path_) + 1) &&
               !signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(&io_), sizeof(io_)) &&
               !signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this));
    }
    Io& io_;
    const char* path_;
};
} // namespace track_sharing
