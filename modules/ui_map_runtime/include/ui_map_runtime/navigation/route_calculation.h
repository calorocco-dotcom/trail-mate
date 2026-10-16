#pragma once
#include "tmap/integer_coordinate_route.h"
#include "tmap/integer_geometry.h"
#include "tmap/outdoor_session.h"
#include "tmap/route_storage.h"

namespace navigation
{
// The platform owns the file/provider and schedules short steps. No LVGL,
// task, global workspace or allocation proportional to the full graph.
class RouteCalculation
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
        Geometry,
        Complete,
        Failed,
        Cancelled
    };
    struct Limits
    {
        size_t states = 4096;
        size_t legs = 2048;
        size_t search_bytes = 160 * 1024;
        uint32_t expansions = 50000;
        double snap_m = 100;
    };
    explicit RouteCalculation(tmap::MemoryProvider& memory, tmap::MemoryPolicy policy = tmap::MemoryPolicy::PreferExternal)
        : session_(memory, policy), storage_(memory, policy), search_(memory, policy), geometry_(memory, policy) {}
    RouteCalculation(const RouteCalculation&) = delete;
    RouteCalculation& operator=(const RouteCalculation&) = delete;
    ~RouteCalculation() { reset(); }

    tmap::Result begin(tmap::RandomAccessFile& file, tmap::GeoPoint origin, tmap::GeoPoint destination,
                       tmap::TravelMode mode, Limits limits)
    {
        reset();
        origin_ = origin;
        destination_ = destination;
        mode_ = mode;
        limits_ = limits;
        tmap::SessionOptions options;
        options.require_graph = true;
        const auto result = session_.begin(file, options);
        if (result != tmap::Result::More) return fail(result);
        phase_ = Phase::Opening;
        return result_ = tmap::Result::More;
    }
    tmap::Result step(size_t budget)
    {
        using R = tmap::Result;
        if (!budget) return R::Invalid;
        R result = result_;
        switch (phase_)
        {
        case Phase::Opening:
            result = session_.initializeStep(budget);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            result = storage_.begin(limits_.states, limits_.legs, limits_.search_bytes);
            if (result != R::More) return fail(result);
            phase_ = Phase::Allocating;
            return result_ = R::More;
        case Phase::Allocating:
            result = storage_.initializeStep(budget);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            result = search_.create();
            if (result != R::Ok) return fail(result);
            phase_ = Phase::Beginning;
            return result_ = R::More;
        case Phase::Beginning:
        {
            tmap::IntegerRouteOptions options;
            options.mode = mode_;
            options.max_expansions = limits_.expansions;
            options.algorithm = tmap::IntegerRouteAlgorithm::HeapDijkstra;
            result = tmap::beginIntegerCoordinateRoute(*session_.graph(), origin_, destination_, options, limits_.snap_m,
                                                       {storage_.labels(), storage_.queue(), storage_.states()},
                                                       storage_.output(), storage_.outputs(), *search_.get());
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            phase_ = Phase::Searching;
            return result_ = R::More;
        }
        case Phase::Searching:
            result = tmap::integerCoordinateRouteStep(*session_.graph(), *search_.get(), budget);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            result = tmap::integerCoordinateRouteResult(*search_.get(), legs_, count_, cost_);
            if (result != R::Ok) return fail(result);
            phase_ = Phase::Ready;
            return result_ = R::Ok;
        case Phase::Geometry:
            result = tmap::integerGeometryStep(*session_.graph(), *geometry_.get(), budget, visitor_, context_);
            if (pending(result)) return result;
            if (result != R::Ok) return fail(result);
            phase_ = Phase::Complete;
            return result_ = R::Ok;
        default:
            return result;
        }
    }
    tmap::Result beginGeometry(tmap::RoutePointVisitor visitor, void* context)
    {
        if (phase_ != Phase::Ready || !visitor) return tmap::Result::Invalid;
        auto result = geometry_.create();
        if (result != tmap::Result::Ok) return fail(result);
        result = tmap::beginIntegerGeometry(*session_.graph(), legs_, count_, *geometry_.get());
        if (result != tmap::Result::Ok) return fail(result);
        visitor_ = visitor;
        context_ = context;
        phase_ = Phase::Geometry;
        return result_ = tmap::Result::More;
    }
    void reset()
    {
        if (search_.get()) tmap::cancel(*search_.get());
        if (geometry_.get()) tmap::cancel(*geometry_.get());
        geometry_.reset();
        search_.reset();
        storage_.reset();
        session_.reset();
        visitor_ = nullptr;
        context_ = nullptr;
        legs_ = nullptr;
        count_ = 0;
        cost_ = 0;
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
    tmap::Result result() const { return result_; }
    size_t legCount() const { return count_; }
    uint64_t costUnits() const { return cost_; }
    static bool pending(tmap::Result result) { return result == tmap::Result::More || result == tmap::Result::Busy; }

  private:
    tmap::Result fail(tmap::Result result)
    {
        reset();
        phase_ = Phase::Failed;
        return result_ = result;
    }
    tmap::OutdoorSession session_;
    tmap::IntegerPreparedRouteStorage storage_;
    tmap::MemoryObject<tmap::IntegerCoordinateRouteCursor> search_;
    tmap::MemoryObject<tmap::IntegerGeometryCursor> geometry_;
    const tmap::IntegerRouteLeg* legs_ = nullptr;
    tmap::RoutePointVisitor visitor_ = nullptr;
    void* context_ = nullptr;
    tmap::GeoPoint origin_{}, destination_{};
    Limits limits_{};
    size_t count_ = 0;
    uint64_t cost_ = 0;
    tmap::TravelMode mode_ = tmap::TravelMode::Foot;
    Phase phase_ = Phase::Idle;
    tmap::Result result_ = tmap::Result::Invalid;
};
} // namespace navigation
