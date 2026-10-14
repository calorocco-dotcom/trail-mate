#pragma once
#include "hostlink/route_install_commit.h"
#include "track_sharing/route_file_check.h"
namespace hostlink::management
{
enum class RouteInstallPhase : uint8_t
{
    Idle,
    Source,
    Existing,
    Readback,
    Installed,
    Missing,
    Failed
};
// Platform owns the immutable upload lease, copied request bindings, canonical
// source/target slots, exclusive hash context and XML names. Allocate this large
// object in transient PSRAM on ESP, never on the scheduler/USB task stack.
template <class Io, class Hash>
class RouteInstallJob
{
  public:
    RouteInstallJob(Io& io, Hash& hash, void* names, size_t capacity,
                    const RouteInstallView& request, const char* source, char* target)
        : io_(io), request_(request), source_(source), target_(target), kml_(io, hash, names, capacity), final_(io, hash) {}
    template <class Guard>
    Status begin(bool install, Guard&& guard)
    {
        if (phase_ != RouteInstallPhase::Idle) return Status::Busy;
        const auto allowed = guard();
        if (allowed != Status::Ok) return fail(allowed);
        if (!source_ || !target_ || !request_.transfer.data || request_.transfer.size != 16 || !request_.hash.data || request_.hash.size != 32 ||
            !request_.bytes || request_.bytes > 8388608 || !request_.generation || request_.generation >= UINT32_MAX) return fail(Status::InvalidField);
        const auto overlaps = [&](const void* data, size_t size)
        {
            const auto a = reinterpret_cast<uintptr_t>(data), b = reinterpret_cast<uintptr_t>(target_);
            return a <= b ? b - a < size : a - b < 77;
        };
        if (overlaps(source_, 64) || overlaps(request_.transfer.data, 16) || overlaps(request_.hash.data, 32) || overlaps(this, sizeof(*this)) || overlaps(&io_, sizeof(io_))) return fail(Status::InvalidField);
        if (!track_sharing::resolveUploadPath(request_.transfer.data, 16, track_sharing::UploadFile::Candidate, target_, 77) ||
            std::strncmp(source_, target_, 64) || !routeInstallTarget(request_.hash, target_, 77)) return fail(Status::InvalidField);
        const auto existing = io_.open(target_, Io::Mode::Read);
        if (existing.handle)
        {
            if (!io_.close(existing.handle)) return fail(Status::StorageUnavailable);
            phase_ = RouteInstallPhase::Existing;
        }
        else
        {
            if (existing.status != Io::OpenStatus::Missing) return fail(Status::StorageUnavailable);
            if (!install)
            {
                phase_ = RouteInstallPhase::Missing;
                return status_ = Status::Ok;
            }
            phase_ = RouteInstallPhase::Source;
        }
        const auto begun = kml_.begin(phase_ == RouteInstallPhase::Existing ? target_ : source_, request_.bytes, request_.hash.data);
        return begun == track_sharing::FileIntegrityResult::Pending ? status_ = Status::Busy : fail(map(begun));
    }
    template <class Guard>
    Status step(uint8_t* scratch, size_t capacity, Guard&& guard)
    {
        if (phase_ == RouteInstallPhase::Failed) return status_;
        const auto allowed = guard();
        if (allowed != Status::Ok)
        {
            kml_.cancel();
            final_.cancel();
            return fail(allowed);
        }
        if (phase_ == RouteInstallPhase::Installed || phase_ == RouteInstallPhase::Missing) return status_;
        if (phase_ == RouteInstallPhase::Idle) return Status::InvalidField;
        if (phase_ == RouteInstallPhase::Readback)
        {
            const auto result = final_.step(scratch, capacity);
            if (result == track_sharing::FileIntegrityResult::Pending) return Status::Busy;
            if (result != track_sharing::FileIntegrityResult::Ready) return fail(map(result));
            phase_ = RouteInstallPhase::Installed;
            return status_ = Status::Ok;
        }
        const auto checked = kml_.step(scratch, capacity);
        if (checked == track_sharing::FileIntegrityResult::Pending) return Status::Busy;
        if (checked != track_sharing::FileIntegrityResult::Ready) return fail(map(checked));
        if (phase_ == RouteInstallPhase::Existing)
        {
            phase_ = RouteInstallPhase::Installed;
            return status_ = Status::Ok;
        }
        const auto published = publishValidatedRoute(io_, kml_, request_, source_, target_, guard);
        if (published != Status::Ok && published != Status::Internal && published != Status::Conflict) return fail(published);
        mutation_possible_ = published != Status::Conflict;
        phase_ = RouteInstallPhase::Readback;
        const auto begun = final_.begin(target_, request_.bytes, request_.hash.data);
        return begun == track_sharing::FileIntegrityResult::Pending ? status_ = Status::Busy : fail(map(begun));
    }
    Status cancel()
    {
        kml_.cancel();
        final_.cancel();
        return fail(mutation_possible_ ? Status::Internal : Status::Expired);
    }
    RouteInstallPhase phase() const { return phase_; }
    uint8_t wireState() const
    {
        if (phase_ == RouteInstallPhase::Installed) return 1;
        if (phase_ == RouteInstallPhase::Missing) return 2;
        return phase_ == RouteInstallPhase::Source || phase_ == RouteInstallPhase::Existing || phase_ == RouteInstallPhase::Readback ? 0 : UINT8_MAX;
    }
    Status status() const { return status_; }

  private:
    static Status map(track_sharing::FileIntegrityResult result)
    {
        return result == track_sharing::FileIntegrityResult::HashMismatch ? Status::HashMismatch : result == track_sharing::FileIntegrityResult::StorageFailure ? Status::StorageUnavailable
                                                                                                                                                                : Status::InvalidField;
    }
    Status fail(Status result)
    {
        phase_ = RouteInstallPhase::Failed;
        return status_ = mutation_possible_ ? Status::Internal : result;
    }
    Io& io_;
    RouteInstallView request_;
    const char* source_;
    char* target_;
    track_sharing::RouteFileCheck<Io, Hash> kml_;
    track_sharing::FileIntegrityCheck<Io, Hash> final_;
    RouteInstallPhase phase_ = RouteInstallPhase::Idle;
    Status status_ = Status::Busy;
    bool mutation_possible_ = false;
};
} // namespace hostlink::management
