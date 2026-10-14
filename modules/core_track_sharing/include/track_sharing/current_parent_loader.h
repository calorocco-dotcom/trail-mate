#pragma once
#include "track_sharing/object_head_store.h"
#include "track_sharing/object_paths.h"
#include "track_sharing/signed_manifest_reader.h"
#include "track_sharing/verify_manifest.h"
namespace track_sharing
{
enum class ParentLoadResult : uint8_t
{
    Idle,
    Pending,
    Ready,
    Missing,
    Conflict,
    Corrupt,
    StorageFailure,
    Invalid
};
// Caller lends immutable expected bindings, current-head slots, paths, signed
// bytes and verification workspace, all disjoint under an exclusive media lease.
template <class Io, class Hash, class Crypto, class Store>
class CurrentParentLoader
{
  public:
    CurrentParentLoader(Io& io, Hash& hash, Crypto& crypto, Store& store, ObjectHead& head, ObjectHead& other,
                        VerifiedManifestView& parent, char* path, uint8_t* raw, size_t capacity)
        : reader_(io), hash_(hash), crypto_(crypto), store_(store), head_(head), other_(other), parent_(parent), path_(path), raw_(raw), capacity_(capacity) {}
    ParentLoadResult begin(geocaching::ByteView object, geocaching::ByteView expected_parent, geocaching::ByteView author, uint32_t generation, uint8_t* scratch, size_t capacity)
    {
        if (result_ != ParentLoadResult::Idle) return ParentLoadResult::Invalid;
        if (!object.data || object.size != 32 || !expected_parent.data || expected_parent.size != 32 || !author.data || author.size != 64 || !generation || !path_ || !raw_ || capacity_ < 4166) return ParentLoadResult::Invalid;
        const auto loaded = store_.load(head_, other_, scratch, capacity);
        if (loaded != HeadStoreResult::Ready)
            return result_ = loaded == HeadStoreResult::Missing ? ParentLoadResult::Missing : loaded == HeadStoreResult::Corrupt      ? ParentLoadResult::Corrupt
                                                                                          : loaded == HeadStoreResult::StorageFailure ? ParentLoadResult::StorageFailure
                                                                                                                                      : ParentLoadResult::Invalid;
        if (head_.phase != HeadPhase::Committed || std::memcmp(head_.object, object.data, 32) || std::memcmp(head_.hash, expected_parent.data, 32)) return result_ = ParentLoadResult::Conflict;
        if (!resolveObjectPath(object, ObjectFile::SignedManifest, {head_.hash, 32}, path_, kObjectPathCapacity)) return result_ = ParentLoadResult::Invalid;
        const auto read = reader_.begin(path_, raw_, capacity_);
        if (read != FileIntegrityResult::Pending) return result_ = read == FileIntegrityResult::StorageFailure ? ParentLoadResult::StorageFailure : ParentLoadResult::Invalid;
        author_ = author;
        generation_ = generation;
        return result_ = ParentLoadResult::Pending;
    }
    ParentLoadResult step(uint8_t* workspace, size_t capacity)
    {
        if (result_ != ParentLoadResult::Pending) return result_;
        const auto read = reader_.step();
        if (read == FileIntegrityResult::Pending) return result_;
        if (read != FileIntegrityResult::Ready) return result_ = ParentLoadResult::StorageFailure;
        const auto current = store_.validateCurrent(head_, other_, workspace, capacity);
        if (current != HeadStoreResult::Ready) return result_ = current == HeadStoreResult::StorageFailure ? ParentLoadResult::StorageFailure : current == HeadStoreResult::Corrupt ? ParentLoadResult::Corrupt
                                                                                                                                                                                    : ParentLoadResult::Conflict;
        if (verifyManifest(reader_.bytes(), crypto_, hash_, workspace, capacity, parent_, author_) != ManifestVerification::Valid) return result_ = ParentLoadResult::Invalid;
        if (parent_.manifest.revision != head_.revision || parent_.manifest.state != (head_.archived ? 1 : 0) ||
            std::memcmp(parent_.object_id, head_.object, 32) || std::memcmp(parent_.revision_hash, head_.hash, 32))
        {
            parent_.verified = false;
            return result_ = ParentLoadResult::Conflict;
        }
        if (head_.generation != generation_)
        {
            const auto rebound = store_.rebindCommittedGeneration(head_, other_, generation_, workspace, capacity);
            if (rebound != HeadStoreResult::Ready)
            {
                parent_.verified = false;
                return result_ = rebound == HeadStoreResult::StorageFailure ? ParentLoadResult::StorageFailure : rebound == HeadStoreResult::Corrupt ? ParentLoadResult::Corrupt
                                                                                                                                                     : ParentLoadResult::Conflict;
            }
        }
        return result_ = ParentLoadResult::Ready;
    }
    void cancel()
    {
        reader_.cancel();
        parent_.verified = false;
        result_ = ParentLoadResult::Invalid;
    }
    bool ready() const { return result_ == ParentLoadResult::Ready && parent_.verified; }

  private:
    SignedManifestReader<Io> reader_;
    Hash& hash_;
    Crypto& crypto_;
    Store& store_;
    ObjectHead& head_;
    ObjectHead& other_;
    VerifiedManifestView& parent_;
    char* path_;
    uint8_t* raw_;
    size_t capacity_;
    geocaching::ByteView author_;
    uint32_t generation_ = 0;
    ParentLoadResult result_ = ParentLoadResult::Idle;
};
} // namespace track_sharing
