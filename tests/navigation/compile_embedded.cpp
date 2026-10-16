#include "ui_map_runtime/navigation/archive_calculation.h"
#include "ui_map_runtime/navigation/saved_route.h"

// Compile the production inline paths with real embedded compilers. Callers
// supply owners so this probe never puts a workspace/session on a task stack.
extern "C" tmap::Result navigationArchiveBegin(navigation::ArchiveCalculation& job, tmap::RandomAccessFile& map,
                                               tmap::WritableRouteFile& spool, tmap::WritableRouteFile& output,
                                               tmap::GeoPoint from, tmap::GeoPoint to,
                                               const navigation::ArchiveCalculation::Limits& limits)
{
    return job.begin(map, spool, output, from, to, limits);
}
extern "C" tmap::Result navigationArchiveStep(navigation::ArchiveCalculation& job, size_t budget)
{
    return job.step(budget);
}
extern "C" tmap::Result navigationArchiveExport(navigation::ArchiveCalculation& job) { return job.beginExport(); }
extern "C" void navigationArchiveCancel(navigation::ArchiveCalculation& job) { job.cancel(); }
extern "C" tmap::Result navigationSavedOpen(navigation::SavedRoute& route, tmap::RandomAccessFile& file)
{
    return route.begin(file);
}
extern "C" tmap::Result navigationSavedOpenStep(navigation::SavedRoute& route, size_t budget) { return route.openStep(budget); }
extern "C" tmap::Result navigationSavedFix(navigation::SavedRoute& route, tmap::GeoPoint point, uint32_t now)
{
    return route.beginFix(point, now);
}
extern "C" tmap::Result navigationSavedStep(navigation::SavedRoute& route, size_t budget) { return route.fixStep(budget); }
extern "C" tmap::Result navigationSavedView(navigation::SavedRoute& route, const tmap::Bounds& bounds)
{
    return route.beginView(bounds);
}
