#pragma once
#include "track_sharing/route_match.h"
#include "track_sharing/route_navigation_state.h"
#include "ui/support/kml_route_identity.h"
#include "ui/support/route_image_kml_reader.h"
namespace ui::support
{
// Owned by the active route, allocated externally. XML work exists only while
// scanning; each poll has a byte budget and an elapsed-time budget between reads.
class KmlNavigationMatch
{
  public:
    KmlRouteIdentity expected;
    KmlNavigationMatch() = default;
    ~KmlNavigationMatch() { cancel(); }
    KmlNavigationMatch(const KmlNavigationMatch&) = delete;
    KmlNavigationMatch& operator=(const KmlNavigationMatch&) = delete;
    static KmlNavigationMatch* create()
    {
#if defined(ESP_PLATFORM)
        const auto caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
        void* memory = heap_caps_malloc(sizeof(KmlNavigationMatch), caps | MALLOC_CAP_8BIT);
#else
        void* memory = std::malloc(sizeof(KmlNavigationMatch));
#endif
        return memory ? new (memory) KmlNavigationMatch : nullptr;
    }
    static void destroy(KmlNavigationMatch*& owner)
    {
        if (!owner) return;
        owner->~KmlNavigationMatch();
#if defined(ESP_PLATFORM)
        heap_caps_free(owner);
#else
        std::free(owner);
#endif
        owner = nullptr;
    }
    void cancel()
    {
        release();
        ready_ = false;
        last_start_ = 0;
        started_ = false;
        navigation_.reset();
    }
    bool pending() const { return work_ != nullptr; }
    const track_sharing::RouteMatchResult* result() const { return ready_ ? &result_ : nullptr; }
    track_sharing::RouteNavigationView navigation(double lat, double lon, uint32_t now) const
    {
        if (!ready_ || !valid_fix(lat, lon)) return {};
        const double movement = track_sharing::routeAngularDistance(result_fix_, {lat, lon}) * track_sharing::routeEarthRadius;
        return navigation_.project(result_, now - completed_at_, movement);
    }
    // Failed parses invalidate earlier results. GPS movement produces rigorous
    // distance bounds: distance-to-a-set is 1-Lipschitz in surface distance.
    bool bounds(double lat, double lon, uint32_t now, double& lower, double& upper) const
    {
        if (!ready_ || now - completed_at_ > 15000 || !valid_fix(lat, lon)) return false;
        const double movement = track_sharing::routeAngularDistance(result_fix_, {lat, lon}) * track_sharing::routeEarthRadius;
        lower = std::max(0.0, result_.distance_m - movement);
        upper = result_.distance_m + movement;
        return true;
    }
    void poll(const char* path, double lat, double lon, uint32_t now, uint32_t (*clock)(), size_t chunks = 64, uint32_t milliseconds = 4)
    {
        if (!valid_fix(lat, lon))
        {
            cancel();
            return;
        }
        if (!work_)
        {
            if (ready_ && now - completed_at_ < 5000 && track_sharing::routeAngularDistance(result_fix_, {lat, lon}) * track_sharing::routeEarthRadius < 1) return;
            if (started_ && now - last_start_ < 1000) return;
            started_ = true;
            last_start_ = now;
            if (!begin(path, {lat, lon}))
            {
                ready_ = false;
                navigation_.reset();
                return;
            }
        }
        const uint32_t start = clock ? clock() : now;
        for (size_t i = 0; i < chunks && work_; ++i)
        {
            uint32_t count = 0;
            if (lv_fs_read(&file_, work_->chunk, sizeof(work_->chunk), &count) != LV_FS_RES_OK || count > sizeof(work_->chunk))
            {
                fail();
                break;
            }
            if (!count)
            {
                const unsigned selected = expected.counts[1] ? 1 : 0;
                const bool valid = work_->parser.finish() && !geometry_failed_[selected] && seen_ == expected;
                const auto candidate = accumulators_[selected].result();
                const bool closed = release();
                if (valid && closed && candidate.valid)
                {
                    result_ = candidate;
                    result_fix_ = scan_fix_;
                    completed_at_ = clock ? clock() : now;
                    ready_ = true;
                    const double movement = track_sharing::routeAngularDistance(result_fix_, {lat, lon}) * track_sharing::routeEarthRadius;
                    navigation_.observe(result_, completed_at_, movement);
                }
                else
                {
                    ready_ = false;
                    navigation_.reset();
                }
                break;
            }
            if (!work_->parser.feed(reinterpret_cast<const uint8_t*>(work_->chunk), count))
            {
                fail();
                break;
            }
            if (clock && clock() - start >= milliseconds) break;
        }
    }

  private:
    static bool valid_fix(double lat, double lon)
    {
        return std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90 && std::abs(lon) <= 180;
    }
    bool begin(const char* path, track_sharing::RouteMatchPoint fix)
    {
        const auto normalized = fs::normalize_path(path);
        if (normalized.empty() || (!expected.counts[0] && !expected.counts[1])) return false;
#if defined(ESP_PLATFORM)
        const auto caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
        void* memory = heap_caps_malloc(sizeof(RouteImageKmlWork), caps | MALLOC_CAP_8BIT);
#else
        void* memory = std::malloc(sizeof(RouteImageKmlWork));
#endif
        if (!memory) return false;
        seen_ = {};
        geometry_failed_[0] = geometry_failed_[1] = false;
        scan_fix_ = fix;
        for (auto& accumulator : accumulators_) accumulator = track_sharing::RouteMatchAccumulator(fix);
        track_sharing::KmlImageCallbacks callbacks{};
        callbacks.context = this;
        callbacks.coordinate = [](void* context, bool gx, double lat, double lon, double altitude, bool height, bool broken)
        {
            auto& self = *static_cast<KmlNavigationMatch*>(context);
            self.seen_.coordinate(gx, lat, lon, altitude, height, broken);
            if (!self.accumulators_[gx ? 1 : 0].point({lat, lon}, broken)) self.geometry_failed_[gx ? 1 : 0] = true;
        };
        callbacks.geometry = [](void* context, bool gx, bool begin, track_sharing::KmlAltitudeMode mode)
        { static_cast<KmlNavigationMatch*>(context)->seen_.geometry(gx, begin, mode); };
        work_ = new (memory) RouteImageKmlWork(callbacks);
        file_open_ = lv_fs_open(&file_, normalized.c_str(), LV_FS_MODE_RD) == LV_FS_RES_OK;
        if (!file_open_)
        {
            release();
            return false;
        }
        return true;
    }
    bool release()
    {
        bool ok = true;
        if (file_open_)
        {
            ok = lv_fs_close(&file_) == LV_FS_RES_OK;
            file_open_ = false;
        }
        if (work_)
        {
            work_->~RouteImageKmlWork();
#if defined(ESP_PLATFORM)
            heap_caps_free(work_);
#else
            std::free(work_);
#endif
            work_ = nullptr;
        }
        return ok;
    }
    void fail()
    {
        release();
        ready_ = false;
        navigation_.reset();
    }
    RouteImageKmlWork* work_ = nullptr;
    lv_fs_file_t file_{};
    KmlRouteIdentity seen_;
    track_sharing::RouteMatchAccumulator accumulators_[2];
    track_sharing::RouteMatchPoint scan_fix_{}, result_fix_{};
    track_sharing::RouteMatchResult result_;
    track_sharing::RouteNavigationState navigation_;
    uint32_t last_start_ = 0, completed_at_ = 0;
    bool geometry_failed_[2]{};
    bool file_open_ = false, ready_ = false, started_ = false;
};
static_assert(sizeof(KmlNavigationMatch) <= 384, "Navigation owner has no retained coordinates or parser");
} // namespace ui::support
