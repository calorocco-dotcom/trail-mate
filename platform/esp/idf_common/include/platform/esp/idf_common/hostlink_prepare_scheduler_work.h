#pragma once
#include "chat/ports/i_mesh_adapter.h"
#include "hostlink/management_envelope.h"
#include "track_sharing/prepare_request.h"
#include <esp_heap_caps.h>
namespace hostlink::management::prepare_runtime
{
struct TrackPreparedResult
{
    uint8_t object[32]{}, hash[32]{}, parent[32]{}, operation[16]{};
    uint32_t revision = 0;
};
// One existing application scheduler pointer, no extra global slot/queue.
// Virtual dispatch and concrete state live only in transient PSRAM.
class PrepareSchedulerWork
{
  public:
    virtual ~PrepareSchedulerWork() = default;
    virtual bool alive(uint32_t, uint64_t, bool) const = 0;
    virtual bool matchesSession(const uint8_t*) const = 0;
    virtual bool holdsStages() const = 0;
    virtual void suspendIdentity() = 0;
    virtual geocaching::ByteView operationId() const = 0;
    virtual bool renewMatched(uint64_t) = 0;
    virtual bool matches(const uint8_t*, const track_sharing::PrepareRequest&) const = 0;
    virtual Status advance(chat::IMeshAdapter&, uint8_t*, size_t) = 0;
    virtual const TrackPreparedResult* result() const = 0;
    virtual geocaching::ByteView preparedSignature() const = 0;
    virtual bool ready() const = 0;
    virtual Status status() const = 0;
    virtual bool packageInstallation() const { return false; }
    virtual uint8_t installationState() const { return ready() ? 1 : 0; }
};
inline void destroySchedulerWork(PrepareSchedulerWork* work)
{
    if (work)
    {
        work->~PrepareSchedulerWork();
        heap_caps_free(work);
    }
}
} // namespace hostlink::management::prepare_runtime
