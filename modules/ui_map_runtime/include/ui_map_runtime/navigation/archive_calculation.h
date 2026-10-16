#pragma once
#include "tmap/integer_coordinate_route.h"
#include "tmap/outdoor_session.h"
#include "tmap/route_archive.h"
#include "tmap/staged_memory.h"

namespace navigation
{
// Application pipeline, independent of LVGL/RTOS/filesystem. The caller owns
// three files for the entire job and publishes the output only after Complete.
// Objects and arrays use the supplied external-memory policy, never implicit heap.
class ArchiveCalculation
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        Opening,
        Allocating,
        Beginning,
        Searching,
        Ready,
        ExportBeginning,
        Exporting,
        Complete,
        Failed,
        Cancelled
    };
    struct Limits
    {
        size_t states = 65536;
        uint32_t legs = 65536;
        size_t search_bytes = 2 * 1024 * 1024;
        uint32_t expansions = 200000;
        uint64_t archive_bytes = 8 * 1024 * 1024;
        double snap_m = 50;
    };
    explicit ArchiveCalculation(tmap::MemoryProvider& memory,
                                tmap::MemoryPolicy policy = tmap::MemoryPolicy::RequireExternal)
        : session_(memory, policy), labels_(memory, policy), queue_(memory, policy), bounds_(memory, policy),
          search_(memory, policy), landmarks_(memory, policy), spool_(memory, policy),
          workspace_(memory, policy), writer_(memory, policy), exporter_(memory, policy) {}
    ~ArchiveCalculation() { reset(); }
    ArchiveCalculation(const ArchiveCalculation&) = delete;
    ArchiveCalculation& operator=(const ArchiveCalculation&) = delete;
    static bool pending(tmap::Result value) { return value == tmap::Result::More || value == tmap::Result::Busy; }

    tmap::Result begin(tmap::RandomAccessFile& map, tmap::WritableRouteFile& spool, tmap::WritableRouteFile& output,
                       tmap::GeoPoint origin, tmap::GeoPoint destination, Limits limits)
    {
        reset();
        if (limits.states < 2 || limits.states > UINT32_MAX || !limits.legs ||
            limits.states > limits.search_bytes / (sizeof(tmap::IntegerRouteLabel) + sizeof(uint32_t)) ||
            !limits.expansions || output.size()) return fail(tmap::Result::Invalid);
        limits_ = limits;
        origin_ = origin;
        destination_ = destination;
        output_ = &output;
        auto result = spool_.create(spool, limits.legs);
        if (result != tmap::Result::Ok) return fail(result);
        tmap::SessionOptions options;
        options.require_graph = true;
        result = session_.begin(map, options);
        if (!pending(result)) return fail(result);
        phase_ = Phase::Opening;
        return result_ = tmap::Result::More;
    }
    tmap::Result step(size_t budget)
    {
        using R = tmap::Result;
        if (!budget) return R::Invalid;
        R result = R::Invalid;
        switch (phase_)
        {
        case Phase::Opening:
            result = session_.initializeStep(budget);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            use_landmarks_ = session_.package()->section(tmap::SectionType::LandmarkPolicy) ||
                             session_.package()->section(tmap::SectionType::LandmarkDistances);
            if (use_landmarks_ && limits_.states > limits_.search_bytes /
                                                       (sizeof(tmap::IntegerRouteLabel) + 2 * sizeof(uint32_t))) return fail(R::BudgetExceeded);
            phase_ = Phase::Allocating;
            return result_ = R::More;
        case Phase::Allocating:
            // Construction, not just reservation, is split into bounded slices.
            while (budget && allocation_ < 6)
            {
                if (allocation_ == 4 && !use_landmarks_)
                {
                    allocation_ = 6;
                    break;
                }
                if (allocation_ == 0) result = labels_.beginReserve(limits_.states);
                else if (allocation_ == 1) result = labels_.initializeStep(1);
                else if (allocation_ == 2) result = queue_.beginReserve(limits_.states);
                else if (allocation_ == 3) result = queue_.initializeStep(1);
                else if (allocation_ == 4) result = bounds_.beginReserve(limits_.states);
                else result = bounds_.initializeStep(1);
                --budget;
                if (!pending(result) && result != R::Ok) return fail(result);
                if (!(allocation_ & 1) || result == R::Ok) ++allocation_;
            }
            if (allocation_ != 6) return result_ = R::More;
            if (use_landmarks_)
            {
                result = landmarks_.create();
                if (result != R::Ok) return fail(result);
            }
            result = search_.create();
            if (result != R::Ok) return fail(result);
            phase_ = Phase::Beginning;
            return result_ = R::More;
        case Phase::Beginning:
        {
            tmap::IntegerRouteOptions options;
            options.mode = tmap::TravelMode::Foot;
            options.algorithm = use_landmarks_ ? tmap::IntegerRouteAlgorithm::Landmarks : tmap::IntegerRouteAlgorithm::HeapDijkstra;
            options.max_expansions = limits_.expansions;
            result = tmap::beginIntegerCoordinateRouteToSink(*session_.graph(), origin_, destination_, options, limits_.snap_m,
                                                             {labels_.data(), queue_.data(), limits_.states,
                                                              bounds_.data(), landmarks_.get()},
                                                             *spool_.get(), limits_.legs, *search_.get());
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            phase_ = Phase::Searching;
            return result_ = R::More;
        }
        case Phase::Searching:
            result = tmap::integerCoordinateRouteStep(*session_.graph(), *search_.get(), budget);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            result = tmap::integerCoordinateRouteSummary(*search_.get(), legs_, cost_);
            if (result != R::Ok) return fail(result);
            releaseSearch();
            phase_ = Phase::Ready;
            return result_ = R::Ok;
        case Phase::ExportBeginning:
            result = writer_.get()->begin(*output_, session_.package()->info(), static_cast<uint32_t>(legs_),
                                          cost_, limits_.archive_bytes);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            result = tmap::beginRouteArchiveExport(*session_.graph(), *spool_.get(), static_cast<uint32_t>(legs_),
                                                   *writer_.get(), *exporter_.get());
            if (result != R::Ok) return fail(result);
            phase_ = Phase::Exporting;
            return result_ = R::More;
        case Phase::Exporting:
            result = tmap::routeArchiveExportStep(*session_.graph(), *spool_.get(), *writer_.get(), *exporter_.get(), budget);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            points_ = writer_.get()->info().points;
            length_cm_ = writer_.get()->info().length_cm;
            release();
            phase_ = Phase::Complete;
            return result_ = R::Ok;
        default:
            return result_;
        }
    }
    tmap::Result beginExport()
    {
        if (phase_ != Phase::Ready) return tmap::Result::Invalid;
        auto result = workspace_.create();
        if (result != tmap::Result::Ok) return fail(result);
        result = writer_.create(*workspace_.get());
        if (result != tmap::Result::Ok) return fail(result);
        result = exporter_.create();
        if (result != tmap::Result::Ok) return fail(result);
        phase_ = Phase::ExportBeginning;
        return result_ = tmap::Result::More;
    }
    void reset()
    {
        release();
        output_ = nullptr;
        legs_ = 0;
        cost_ = points_ = length_cm_ = 0;
        allocation_ = 0;
        phase_ = Phase::Idle;
        result_ = tmap::Result::Invalid;
    }
    void cancel()
    {
        reset();
        phase_ = Phase::Cancelled;
        result_ = tmap::Result::Cancelled;
    }
    Phase phase() const { return phase_; }
    size_t legCount() const { return legs_; }
    uint64_t costUnits() const { return cost_; }
    uint64_t lengthCm() const { return length_cm_; }
    uint64_t pointCount() const { return points_; }
    size_t reservedBytes() const
    {
        return session_.reservedBytes() + labels_.bytes() + queue_.bytes() + bounds_.bytes() +
               search_.bytes() + landmarks_.bytes() + spool_.bytes() + workspace_.bytes() + writer_.bytes() + exporter_.bytes();
    }

  private:
    void releaseSearch()
    {
        search_.reset();
        landmarks_.reset();
        bounds_.reset();
        queue_.reset();
        labels_.reset();
    }
    void release()
    {
        exporter_.reset();
        writer_.reset();
        workspace_.reset();
        releaseSearch();
        spool_.reset();
        session_.reset();
    }
    tmap::Result fail(tmap::Result result)
    {
        release();
        phase_ = Phase::Failed;
        return result_ = result;
    }
    tmap::OutdoorSession session_;
    tmap::StagedMemoryBuffer<tmap::IntegerRouteLabel> labels_;
    tmap::StagedMemoryBuffer<uint32_t> queue_, bounds_;
    tmap::MemoryObject<tmap::IntegerCoordinateRouteCursor> search_;
    tmap::MemoryObject<tmap::IntegerLandmarkWorkspace> landmarks_;
    tmap::MemoryObject<tmap::RouteLegSpool> spool_;
    tmap::MemoryObject<tmap::RouteArchiveWorkspace> workspace_;
    tmap::MemoryObject<tmap::RouteArchiveWriter> writer_;
    tmap::MemoryObject<tmap::RouteArchiveExportCursor> exporter_;
    tmap::WritableRouteFile* output_ = nullptr;
    tmap::GeoPoint origin_{}, destination_{};
    Limits limits_{};
    size_t legs_ = 0;
    uint64_t cost_ = 0, points_ = 0, length_cm_ = 0;
    uint8_t allocation_ = 0;
    bool use_landmarks_ = false;
    Phase phase_ = Phase::Idle;
    tmap::Result result_ = tmap::Result::Invalid;
};
} // namespace navigation
