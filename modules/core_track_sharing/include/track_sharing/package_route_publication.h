#pragma once
#include "track_sharing/immutable_file_copy.h"
#include "track_sharing/installed_package_binding.h"
namespace track_sharing
{
enum class PackageRouteResult : uint8_t
{
    Idle,
    Pending,
    Ready,
    Invalid,
    Conflict,
    StorageFailure,
    HashMismatch,
    Cancelled
};
// Caller first proves the public cache, then retains source/identity/media and
// active-route/storage leases. Record is committed before KML rename, making
// .kml publication the final visibility step. No directory-wide rollback.
template <class Io, class Hash>
class PackageRoutePublication
{
  public:
    PackageRoutePublication(Io& io, Hash& hash, const InstalledPackageBinding& binding, const char* cached_kml)
        : io_(io), binding_(binding), source_(cached_kml), copy_(io, hash) {}
    PackageRouteResult begin()
    {
        if (result_ != PackageRouteResult::Idle) return PackageRouteResult::Conflict;
        if (!source_ || !*source_ || !installedPackageBindingValid(binding_) ||
            !routePackageTarget({binding_.revision, 32}, PackageRouteFile::Kml, target_, sizeof(target_)) ||
            !routePackageTarget({binding_.revision, 32}, PackageRouteFile::Temporary, temporary_, sizeof(temporary_)) ||
            !routePackageTarget({binding_.revision, 32}, PackageRouteFile::Binding, record_, sizeof(record_)) ||
            !routePackageTarget({binding_.revision, 32}, PackageRouteFile::BindingTemporary, record_temporary_, sizeof(record_temporary_))) return result_ = PackageRouteResult::Invalid;
        return result_ = PackageRouteResult::Pending;
    }
    template <class Guard>
    PackageRouteResult step(uint8_t* scratch, size_t capacity, Guard&& guard)
    {
        if (result_ != PackageRouteResult::Pending) return result_;
        if (!scratch || capacity < 448 || signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(this), sizeof(*this)) ||
            signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(&binding_), sizeof(binding_)) ||
            signingSpansOverlap(scratch, capacity, reinterpret_cast<const uint8_t*>(source_), std::strlen(source_) + 1)) return fail(PackageRouteResult::Invalid);
        if (!guard()) return fail(PackageRouteResult::Cancelled);
        if (!record_ready_)
        {
            const auto stored = storeRecord(scratch, capacity);
            if (stored != PackageRouteResult::Ready) return fail(stored);
            record_ready_ = true;
            return result_;
        }
        if (!copy_started_)
        {
            const auto begun = copy_.begin(source_, temporary_, target_, binding_.kml_bytes, binding_.kml_hash);
            if (begun != ImmutableCopyResult::Pending) return fail(map(begun));
            copy_started_ = true;
        }
        const auto copied = copy_.step(scratch, capacity);
        if (!guard()) return fail(PackageRouteResult::Cancelled);
        return copied == ImmutableCopyResult::Pending ? result_ : result_ = map(copied);
    }
    void cancel()
    {
        copy_.cancel();
        result_ = PackageRouteResult::Cancelled;
    }
    bool ready() const { return result_ == PackageRouteResult::Ready; }
    const char* target() const { return target_; }

  private:
    PackageRouteResult probe(const char* path, uint8_t* expected, uint8_t* readback, size_t& bytes)
    {
        const auto opened = io_.open(path, Io::Mode::Read);
        if (!opened.handle) return opened.status == Io::OpenStatus::Missing ? PackageRouteResult::Pending : PackageRouteResult::StorageFailure;
        uint64_t size = 0;
        const bool checked = io_.size(opened.handle, size) && size <= kInstalledPackageBindingBytes && io_.read(opened.handle, readback, size) == size;
        const bool closed = io_.close(opened.handle);
        if (!checked || !closed) return PackageRouteResult::StorageFailure;
        bytes = size;
        return std::memcmp(expected, readback, size) ? PackageRouteResult::Conflict : PackageRouteResult::Ready;
    }
    PackageRouteResult storeRecord(uint8_t* scratch, size_t capacity)
    {
        if (!encodeInstalledPackageBinding(binding_, scratch, capacity)) return PackageRouteResult::Invalid;
        auto* readback = scratch + kInstalledPackageBindingBytes;
        size_t bytes = 0;
        const auto final = probe(record_, scratch, readback, bytes);
        if (final == PackageRouteResult::Ready) return bytes == kInstalledPackageBindingBytes ? final : PackageRouteResult::Conflict;
        if (final != PackageRouteResult::Pending) return final;
        bytes = 0;
        const auto temporary = probe(record_temporary_, scratch, readback, bytes);
        if (temporary != PackageRouteResult::Ready && temporary != PackageRouteResult::Pending) return temporary;
        const auto opened = io_.open(record_temporary_, temporary == PackageRouteResult::Pending ? Io::Mode::CreateTemporary : Io::Mode::Update);
        if (!opened.handle) return PackageRouteResult::StorageFailure;
        const bool written = io_.seek(opened.handle, bytes) && io_.write(opened.handle, scratch + bytes, kInstalledPackageBindingBytes - bytes) == kInstalledPackageBindingBytes - bytes && io_.sync(opened.handle);
        const bool closed = io_.close(opened.handle);
        if (!written || !closed) return PackageRouteResult::StorageFailure;
        // A failed rename can already have completed. Exact readback decides.
        io_.publish(record_temporary_, record_);
        bytes = 0;
        const auto stored = probe(record_, scratch, readback, bytes);
        return stored == PackageRouteResult::Ready && bytes == kInstalledPackageBindingBytes ? stored : stored == PackageRouteResult::Conflict ? stored
                                                                                                                                               : PackageRouteResult::StorageFailure;
    }
    static PackageRouteResult map(ImmutableCopyResult result) { return result == ImmutableCopyResult::Ready ? PackageRouteResult::Ready : result == ImmutableCopyResult::HashMismatch ? PackageRouteResult::HashMismatch
                                                                                                                                      : result == ImmutableCopyResult::Conflict       ? PackageRouteResult::Conflict
                                                                                                                                      : result == ImmutableCopyResult::Invalid        ? PackageRouteResult::Invalid
                                                                                                                                                                                      : PackageRouteResult::StorageFailure; }
    PackageRouteResult fail(PackageRouteResult result)
    {
        copy_.cancel();
        return result_ = result;
    }
    Io& io_;
    const InstalledPackageBinding& binding_;
    const char* source_;
    ImmutableFileCopy<Io, Hash> copy_;
    char target_[80]{}, temporary_[80]{}, record_[80]{}, record_temporary_[80]{};
    PackageRouteResult result_ = PackageRouteResult::Idle;
    bool record_ready_ = false, copy_started_ = false;
};
} // namespace track_sharing
