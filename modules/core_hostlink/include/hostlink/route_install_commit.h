#pragma once
#include "hostlink/route_install_request.h"
#include "track_sharing/upload_paths.h"
namespace hostlink::management
{
// Caller lends disjoint 64/77-byte canonical path slots and holds the immutable
// source lease. Guard reads current identity, upload owner, media and active-file
// restrictions. This function never copies bytes or replaces an existing route.
// Ready file proof must match this exact canonical source, length and hash.
template <class Io, class Check, class Guard>
Status publishValidatedRoute(Io& io, const Check& check, const RouteInstallView& request,
                             const char* source, char* target, Guard&& guard)
{
    if (!source || !target) return Status::InvalidField;
    const auto overlaps = [&](const void* input, size_t size)
    {
        if (!input || !size) return false;
        const auto a = reinterpret_cast<uintptr_t>(input), b = reinterpret_cast<uintptr_t>(target);
        return a <= b ? b - a < size : a - b < 77;
    };
    if (overlaps(source, 64) || overlaps(request.transfer.data, request.transfer.size) || overlaps(request.hash.data, request.hash.size) ||
        overlaps(&request, sizeof(request)) || overlaps(&check, sizeof(check)) || overlaps(&io, sizeof(io))) return Status::InvalidField;
    // Generate the canonical source into target scratch and compare; never
    // rewrite the path retained by the completed validation job.
    if (!track_sharing::resolveUploadPath(request.transfer.data, request.transfer.size, track_sharing::UploadFile::Candidate, target, 77) ||
        std::strncmp(source, target, 64) || !check.matchesSource(source, request.bytes, request.hash.data) ||
        !routeInstallTarget(request.hash, target, 77)) return Status::InvalidField;
    auto allowed = guard();
    if (allowed != Status::Ok) return allowed;
    const auto destination = io.open(target, Io::Mode::Read);
    if (destination.handle)
    {
        if (!io.close(destination.handle)) return Status::StorageUnavailable;
        // Existing is not proof of installation: recovery must verify its bytes.
        return Status::Conflict;
    }
    if (destination.status != Io::OpenStatus::Missing) return Status::StorageUnavailable;
    const auto original = io.open(source, Io::Mode::Update);
    if (!original.handle) return Status::StorageUnavailable;
    uint64_t bytes = 0;
    const bool synced = io.size(original.handle, bytes) && bytes == request.bytes && io.sync(original.handle);
    const bool closed = io.close(original.handle);
    if (!synced || !closed) return Status::StorageUnavailable;
    allowed = guard();
    if (allowed != Status::Ok) return allowed;
    // Failure may follow a completed disk mutation. Do not report rejection or
    // roll back/remove a possibly installed route. Retain binding for recovery.
    return io.publish(source, target) ? Status::Ok : Status::Internal;
}
} // namespace hostlink::management
