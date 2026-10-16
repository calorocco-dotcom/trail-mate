#pragma once
#include "platform/ui/gps_runtime.h"
#include "platform/ui/navigation_runtime.h"
#include "ui_map_runtime/navigation/saved_route.h"
#include <cstdio>
#include <new>

namespace ui::support
{
// UI policy and ownership, independent of drawing. Storage/search run through
// the existing asynchronous platform port. A candidate is not activated until
// the UI has opened/validated it; failures leave the current saved route intact.
class NativeNavigationSession
{
  public:
    enum class Phase : uint8_t
    {
        Following,
        Replanning,
        Applying,
        Failed,
        Arrived,
        Stopped
    };
    enum class Action : uint8_t
    {
        None,
        ApplyCandidate,
        Finish
    };
    NativeNavigationSession()
    {
        automatic_ = platform::ui::navigation::supported();
        platform::ui::gps::acquire_power_lease("navigation-follow");
    }
    ~NativeNavigationSession() { stop(); }
    NativeNavigationSession(const NativeNavigationSession&) = delete;
    NativeNavigationSession& operator=(const NativeNavigationSession&) = delete;
    void destination(tmap::GeoPoint point) { destination_ = point; }
    Phase phase() const { return phase_; }
    platform::ui::navigation::Status failure() const { return failure_; }
    bool automatic() const { return automatic_; }
    void setAutomatic(bool enabled) { automatic_ = enabled && platform::ui::navigation::supported(); }
    const char* candidatePath() const { return job_.path; }

    bool replan(tmap::GeoPoint fix, bool valid, uint32_t age, uint32_t now)
    {
        if (phase_ == Phase::Replanning || phase_ == Phase::Applying || phase_ == Phase::Arrived || phase_ == Phase::Stopped ||
            !platform::ui::navigation::supported() || !valid || age > 5000 || fix.latitude < -900000000 || fix.latitude > 900000000 ||
            fix.longitude < -1800000000 || fix.longitude > 1800000000) return false;
        namespace nav = platform::ui::navigation;
        nav::cancel();
        ::new (static_cast<void*>(&job_)) nav::Snapshot{};
        request_.origin.latitude_e7 = fix.latitude;
        request_.origin.longitude_e7 = fix.longitude;
        request_.destination.latitude_e7 = destination_.latitude;
        request_.destination.longitude_e7 = destination_.longitude;
        std::snprintf(request_.origin.name, sizeof(request_.origin.name), "Current location");
        std::snprintf(request_.destination.name, sizeof(request_.destination.name), "Route end");
        last_attempt_ = now;
        attempted_ = off_route_latched_ = true;
        save_attempts_ = 0;
        if (!nav::submit(request_))
        {
            phase_ = Phase::Failed;
            failure_ = nav::Status::MemoryUnavailable;
            return false;
        }
        owns_job_ = true;
        phase_ = Phase::Replanning;
        return true;
    }
    Action tick(const navigation::Guidance& guide, tmap::GeoPoint fix, bool valid, uint32_t age, uint32_t now)
    {
        namespace nav = platform::ui::navigation;
        if (phase_ == Phase::Stopped || phase_ == Phase::Arrived) return Action::None;
        const bool fresh = now - guide.observed_at <= 5000;
        if (fresh && guide.status == navigation::GuidanceStatus::Arrived && valid && age <= 5000)
        {
            cancelOwnedJob();
            phase_ = Phase::Arrived;
            releaseGps();
            return Action::Finish;
        }
        if (phase_ == Phase::Applying) return Action::None;
        if (phase_ == Phase::Replanning)
        {
            nav::poll(job_);
            // Ready can be published just before the worker clears running.
            // Retry save even when poll has no new revision, with a finite limit.
            if (job_.status == nav::Status::Ready)
            {
                if (nav::save()) nav::poll(job_);
                else if (++save_attempts_ >= 10)
                {
                    failure_ = nav::Status::StorageUnavailable;
                    phase_ = Phase::Failed;
                    cancelOwnedJob();
                }
            }
            if (job_.status == nav::Status::Saved)
            {
                phase_ = Phase::Applying;
                cancelOwnedJob();
                return Action::ApplyCandidate;
            }
            if (job_.status != nav::Status::Idle && job_.status != nav::Status::Calculating && job_.status != nav::Status::Ready)
            {
                failure_ = job_.status;
                phase_ = Phase::Failed;
                cancelOwnedJob();
            }
            return Action::None;
        }
        if (fresh && guide.status == navigation::GuidanceStatus::Following)
        {
            off_route_latched_ = false;
            phase_ = Phase::Following;
        }
        if (fresh && automatic_ && phase_ == Phase::Following && !off_route_latched_ &&
            guide.status == navigation::GuidanceStatus::OffRoute && (!attempted_ || now - last_attempt_ >= 30000))
            replan(fix, valid, age, now);
        return Action::None;
    }
    void applied(bool success)
    {
        if (phase_ != Phase::Applying) return;
        phase_ = success ? Phase::Following : Phase::Failed;
        if (!success) failure_ = platform::ui::navigation::Status::StorageUnavailable;
        // One automatic attempt per continuous deviation. A fresh match on the
        // new/current path rearms it; manual retry is always available.
        off_route_latched_ = true;
    }
    void cancelReplan()
    {
        cancelOwnedJob();
        if (phase_ == Phase::Replanning || phase_ == Phase::Applying || phase_ == Phase::Failed) phase_ = Phase::Following;
        off_route_latched_ = true;
    }
    void stop()
    {
        cancelOwnedJob();
        phase_ = Phase::Stopped;
        releaseGps();
    }

  private:
    void cancelOwnedJob()
    {
        if (owns_job_) platform::ui::navigation::cancel();
        owns_job_ = false;
    }
    void releaseGps()
    {
        if (gps_lease_) platform::ui::gps::release_power_lease("navigation-follow");
        gps_lease_ = false;
    }
    platform::ui::navigation::Request request_{};
    platform::ui::navigation::Snapshot job_{};
    tmap::GeoPoint destination_{};
    platform::ui::navigation::Status failure_ = platform::ui::navigation::Status::Idle;
    uint32_t last_attempt_ = 0;
    Phase phase_ = Phase::Following;
    uint8_t save_attempts_ = 0;
    bool automatic_ = true, attempted_ = false, off_route_latched_ = false, owns_job_ = false, gps_lease_ = true;
};
} // namespace ui::support
