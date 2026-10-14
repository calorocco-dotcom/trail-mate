#pragma once
#include "platform/esp/idf_common/hostlink_track_publication_request.h"
#include "platform/esp/idf_common/hostlink_track_read_owner.h"
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
#include "platform/esp/idf_common/hostlink_track_history_owner.h"
#endif
namespace hostlink::management::publication_runtime
{
// Only one resident pointer (4 bytes on ESP). All scheduler/control records are
// admitted PSRAM, and accepted execution outlives its USB request metadata.
struct Controller
{
    chat::IMeshAdapter* adapter = nullptr;
    TrackPublicationRequestWork* request = nullptr;
    TrackPublicationExecution* execution = nullptr;
    track_read_runtime::TrackReadOwner* read = nullptr;
    track_sharing::ReadExchangeState read_state = track_sharing::ReadExchangeState::Idle;
    bool read_retiring = false;
    bool read_directory_cached = false;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    HistoryOwner* history = nullptr;
#endif
    track_sharing::PublicationDirectory directory;
    track_sharing::PublicationJournal latest;
    track_sharing::CommitRequest decoded;
    uint8_t local[16]{};
    uint64_t last = 0;
    bool accepted = false, retiring = false;
    bool history_cached = false;
};
static_assert(sizeof(Controller) <= 512, "Publication controller must retain metadata only");
inline Controller* controller = nullptr;
inline bool holdsStorage()
{
    return controller && (controller->read || controller->execution || (controller->request && controller->request->holdsAdmission())
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
        || controller->history
#endif
    );
}
inline bool destroyController()
{
    if (!controller) return true;
    if (!track_read_runtime::TrackReadOwner::release(controller->read)) return false;
    if (!TrackPublicationExecution::release(controller->execution)) return false;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    releaseHistory(controller->history);
#endif
    releasePublicationRequest(controller->request);
    controller->~Controller();
    heap_caps_free(controller);
    controller = nullptr;
    return true;
}
inline void disconnect()
{
    if (!controller) return;
    releasePublicationRequest(controller->request);
    if (!controller->execution) destroyController();
}
inline void closeSession(const uint8_t* session)
{
    if (controller && controller->read_directory_cached && session && !std::memcmp(controller->local,session,16))
    {
        if (controller->read) controller->read_retiring = true;
        disconnect();
        return;
    }
    if (controller && controller->read && controller->read->matchesSession(session))
    {
        controller->read_retiring = true;
        disconnect();
        return;
    }
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    if(controller && controller->history && controller->history->matchesSession(session))releaseHistory(controller->history);
#endif
    if (controller && controller->request && controller->request->matchesSession(session)) disconnect();
}
inline void advancePending(chat::IMeshAdapter* adapter, uint8_t* scratch, size_t capacity, uint64_t now, bool connected)
{
    if (!connected)
    {
        disconnect();
        return;
    }
    if (controller && controller->request && !controller->execution)
        advancePublicationRequest(controller->request, adapter, scratch, capacity, now, true);
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    if(controller && controller->history && adapter==controller->adapter)
        controller->history->advance(*adapter,scratch,capacity);
#endif
}
inline void tick(uint64_t now)
{
    if (!controller) return;
    if (controller->read)
    {
        if (!controller->read_retiring)
        {
            auto state = controller->read_state;
            const auto changed = controller->read->advance(now, state);
            if (changed == chat::TrackBindingChange::Applied)
            {
                if (state != controller->read_state) controller->last = now;
                controller->read_state = state;
                if (state != track_sharing::ReadExchangeState::Waiting && state != track_sharing::ReadExchangeState::Ready)
                    controller->read_retiring = true;
            }
            else if (changed != chat::TrackBindingChange::Busy) controller->read_retiring = true;
            if (state == track_sharing::ReadExchangeState::Ready && (now < controller->last || now - controller->last >= 30000))
                controller->read_retiring = true;
        }
        if (controller->read_retiring && track_read_runtime::TrackReadOwner::release(controller->read))
            destroyController();
        return;
    }
    if (controller->execution)
    {
        if (!controller->retiring)
        {
            const auto result = controller->execution->advance(now);
            controller->execution->snapshot(controller->latest);
            if (result == track_sharing::NetworkSessionResult::Confirmed || result == track_sharing::NetworkSessionResult::Conflict ||
                result == track_sharing::NetworkSessionResult::StorageFailure || result == track_sharing::NetworkSessionResult::Paused) controller->retiring = true;
        }
        if (controller->retiring && TrackPublicationExecution::release(controller->execution))
        {
            releasePublicationRequest(controller->request);
            controller->last = now;
        }
    }
    else if (!controller->request && (now < controller->last || now - controller->last >= 30000)) destroyController();
}
inline Status encodeAdmission(const track_sharing::PublicationJournal& result, uint8_t* output, size_t capacity, size_t& written)
{
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(8) || !writer.binary({result.operation, 16}) || !writer.binary({result.object, 32}) || !writer.binary({result.revision_hash, 32}) ||
        !writer.unsignedInteger(result.generation) || !writer.unsignedInteger(result.revision) || !writer.binary({result.directory, 16}) ||
        !writer.binary({result.directory_epoch, 16}) || !writer.unsignedInteger(1)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
// JobGet [operation, generation, 1] selects tracks. Retained memory is evidence
// from the authenticated executor only; missing history must not imply success.
inline Status getStatus(chat::IMeshAdapter& adapter, geocaching::ByteView operation, uint32_t generation,
                        uint8_t* output, size_t capacity, size_t& written, const uint8_t* session=nullptr, uint64_t now=0)
{
    written = 0;
    if (!output || capacity < 448 || !operation.data || operation.size != 16 || !generation) return Status::InvalidField;
    if (controller && controller->read) return Status::Busy;
    if (!controller || (!controller->accepted && !controller->history_cached) || controller->adapter != &adapter ||
        std::memcmp(operation.data, controller->latest.operation, 16))
    {
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
        if(!session)return Status::InvalidField;
        if(controller && (controller->adapter!=&adapter || controller->execution || controller->request))return Status::Busy;
        if(!controller)
        {
            void* memory=heap_caps_malloc(sizeof(Controller),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(!memory)return Status::Busy;
            controller=new(memory) Controller;controller->adapter=&adapter;
        }
        controller->last=now;
        if(controller->history && !controller->history->matches(operation,generation,session))releaseHistory(controller->history);
        if(!controller->history)controller->history=createHistory(operation,generation,session);
        if(!controller->history)return Status::Busy;
        const auto status=controller->history->step(adapter,output,capacity);
        if(status!=Status::Ok){if(status!=Status::Busy)releaseHistory(controller->history);return status;}
        if(!controller->history->result())return Status::Busy;
        controller->latest=*controller->history->result();
        controller->history_cached=true;
        releaseHistory(controller->history);
#else
        (void)session;(void)now;
        return Status::Expired;
#endif
    }
    if(controller->history_cached && now)controller->last=now;
    if (generation != controller->latest.generation) return Status::StaleIdentity;
    if (controller->execution)
    {
        const auto result = controller->execution->snapshot(controller->latest);
        if (result != chat::TrackBindingChange::Applied) return result == chat::TrackBindingChange::Busy ? Status::Busy : Status::StaleIdentity;
    }
    else
    {
        uint8_t hash[32];
        uint32_t actual = 0;
        const auto result = upload_runtime::readUploadAuthor(adapter, output + capacity - 64, 64, hash, actual);
        if (result != Status::Ok) return result;
        if (actual != generation || std::memcmp(hash, controller->latest.author_hash, 32)) return Status::StaleIdentity;
    }
    const auto& job = controller->latest;
    geocaching::protocol::CmpWriter writer(output, capacity - 64);
    if (!writer.array(14) || !writer.binary({job.operation, 16}) || !writer.binary({job.object, 32}) ||
        !writer.binary({job.revision_hash, 32}) || !writer.unsignedInteger(job.generation) || !writer.unsignedInteger(job.revision) ||
        !writer.binary({job.directory, 16}) || !writer.binary({job.directory_epoch, 16}) || !writer.binary({job.file_hash, 32}) ||
        !writer.unsignedInteger(job.file_bytes) || !writer.unsignedInteger(job.offset) || !writer.unsignedInteger(uint8_t(job.phase)) ||
        !writer.unsignedInteger(job.sequence) || !writer.binary({job.author_hash, 32}) ||
        !writer.unsignedInteger(controller->execution && !controller->retiring ? 1 : 0)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
inline Status cancelStatus(chat::IMeshAdapter& adapter,geocaching::ByteView operation,uint32_t generation,
                           uint8_t* output,size_t capacity,size_t& written)
{
    written=0;
    if(!operation.data || operation.size!=16 || !generation || !output || capacity<448)return Status::InvalidField;
    if(!controller)return Status::Expired;
    if(controller->adapter!=&adapter)return Status::Conflict;
    if(controller->execution)
    {
        const auto status=controller->execution->cancel(operation,generation,controller->latest);
        if(status!=Status::Ok)return status;
        controller->retiring=true;
    }
    else
    {
        if(std::memcmp(operation.data,controller->latest.operation,16))return Status::Expired;
        if(generation!=controller->latest.generation)return Status::StaleIdentity;
        if(controller->latest.phase!=track_sharing::PublicationPhase::Cancelled)
            return controller->latest.phase==track_sharing::PublicationPhase::Publishing || controller->latest.phase==track_sharing::PublicationPhase::Confirmed?
                Status::Conflict:Status::Unsupported;
    }
    return getStatus(adapter,operation,generation,output,capacity,written);
}
inline Status processCommit(chat::IMeshAdapter& adapter, const EnvelopeView& request, uint64_t now, uint8_t* scratch, size_t capacity, size_t& written)
{
    written = 0;
    if (!request.session || request.code != uint16_t(Operation::TrackCommit) || !scratch || capacity < 448) return Status::InvalidField;
    if (!controller)
    {
        void* memory = heap_caps_malloc(sizeof(Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return Status::Busy;
        controller = new (memory) Controller;
        controller->adapter = &adapter;
        controller->last = now;
    }
    auto& state = *controller;
    if (state.read) return Status::Busy;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    if(state.history)return Status::Busy;
#endif
    if (state.adapter != &adapter) return Status::Conflict;
    if (!track_sharing::decodeCommitRequest({request.body, request.body_size}, state.decoded)) return Status::InvalidField;
    if (state.accepted)
    {
        const auto& decoded = state.decoded;
        if (decoded.generation != state.latest.generation || decoded.revision != state.latest.revision ||
            std::memcmp(decoded.operation.data, state.latest.operation, 16) || std::memcmp(decoded.object.data, state.latest.object, 32) ||
            std::memcmp(decoded.revision_hash.data, state.latest.revision_hash, 32) || std::memcmp(decoded.directory.data, state.latest.directory, 16) ||
            std::memcmp(decoded.directory_epoch.data, state.latest.directory_epoch, 16)) return Status::Conflict;
        if (state.execution)
        {
            const auto snapshot = state.execution->snapshot(state.latest);
            if (snapshot != chat::TrackBindingChange::Applied) return snapshot == chat::TrackBindingChange::Busy ? Status::Busy : Status::StaleIdentity;
        }
        else
        {
            uint8_t hash[32];
            uint32_t generation = 0;
            const auto identity = upload_runtime::readUploadAuthor(adapter, scratch + capacity - 64, 64, hash, generation);
            if (identity != Status::Ok) return identity;
            if (generation != state.latest.generation || std::memcmp(hash, state.latest.author_hash, 32)) return Status::StaleIdentity;
        }
        state.last = now;
        return encodeAdmission(state.latest, scratch, capacity, written);
    }
    const auto pinned = directory_runtime::pin(adapter, request.session, state.decoded.directory, state.decoded.directory_epoch, now, state.directory);
    if (pinned != Status::Ok) return pinned;
    const auto prepared = processPublicationRequest(adapter, state.request, request, state.directory, now, scratch, capacity, written, true);
    if (prepared != Status::Ok) return prepared;
    state.latest = *state.request->result();
    written = 0;
    if (!adapter.trackLocalDestination(state.local)) return Status::Busy;
    chat::TrackBindingChange changed;
    state.execution = state.request->startExecution(adapter, state.local, changed);
    if (!state.execution) return changed == chat::TrackBindingChange::Busy ? Status::Busy : Status::Conflict;
    state.accepted = true;
    state.last = now;
    return encodeAdmission(state.latest, scratch, capacity, written);
}
// Typed entry point for USB decoding. The caller checks snapshot/upload/prepare
// exclusion first; this controller enforces publication/history/read exclusion.
// Pin the directory again here: a browser-supplied public key is never trusted.
inline Status beginRead(chat::IMeshAdapter& adapter, uint8_t operation, geocaching::ByteView request,
                        geocaching::ByteView session, geocaching::ByteView delivery, geocaching::ByteView epoch,
                        geocaching::ByteView author, uint32_t generation,
                        const track_sharing::ReadValidationContext& expected, uint64_t now)
{
    if (!track_sharing::networkRequestId(request) || !track_sharing::networkRequestId(session)) return Status::InvalidField;
    if (controller && (controller->read || controller->execution || controller->request
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
        || controller->history
#endif
        )) return Status::Busy;
    const bool cached = controller && controller->read_directory_cached && controller->adapter == &adapter &&
        now >= controller->last && now-controller->last < 30000 && !std::memcmp(controller->local,session.data,16) &&
        track_sharing::readSpan(delivery,16) && track_sharing::readSpan(epoch,16) &&
        !std::memcmp(controller->directory.delivery,delivery.data,16) && !std::memcmp(controller->directory.epoch,epoch.data,16);
    if (!cached)
    {
        if (controller && !destroyController()) return Status::Busy;
        void* memory = heap_caps_malloc(sizeof(Controller), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return Status::Busy;
        controller = new (memory) Controller;
        controller->adapter = &adapter;
        const auto pinned = directory_runtime::pin(adapter, session.data, delivery, epoch, now, controller->directory);
        if (pinned != Status::Ok) { destroyController(); return pinned; }
    }
    controller->last = now;
    controller->read = track_read_runtime::TrackReadOwner::create(adapter, operation, request, session, controller->directory, author, generation, expected);
    if (!controller->read) { destroyController(); return Status::Busy; }
    const auto changed = directory_runtime::handoverRead(adapter, *controller->read, now);
    if (changed != chat::TrackBindingChange::Applied)
    {
        destroyController();
        return changed == chat::TrackBindingChange::Busy ? Status::Busy : changed == chat::TrackBindingChange::Unsupported ? Status::Unsupported : Status::Conflict;
    }
    controller->read_state = track_sharing::ReadExchangeState::Waiting;
    controller->read_retiring = false;
    controller->read_directory_cached = true;
    std::memcpy(controller->local,session.data,16);
    return Status::Ok;
}
inline Status readResult(chat::IMeshAdapter& adapter, geocaching::ByteView session, geocaching::ByteView request,
                         uint32_t offset, uint16_t count, uint8_t* output, size_t capacity, size_t& written,
                         track_sharing::StoredReadResponse& metadata, uint64_t now)
{
    written = 0;
    metadata = {};
    if (!track_sharing::networkRequestId(session) || !track_sharing::networkRequestId(request)) return Status::InvalidField;
    if (!controller || !controller->read || controller->read_retiring) return Status::Expired;
    if (controller->adapter != &adapter || !controller->read->matchesSession(session.data)) return Status::Unauthorized;
    if (!controller->read->matchesRequest(request)) return Status::Conflict;
    track_sharing::ReadStoreResult result;
    const auto changed = controller->read->read(offset,count,output,capacity,written,metadata,result);
    if (changed != chat::TrackBindingChange::Applied) return changed == chat::TrackBindingChange::Busy ? Status::Busy : Status::StaleIdentity;
    if (result == track_sharing::ReadStoreResult::Ready) { controller->last = now; return Status::Ok; }
    if (result == track_sharing::ReadStoreResult::Invalid) return metadata.ready ? Status::InvalidField : Status::Busy;
    return result == track_sharing::ReadStoreResult::Conflict ? Status::Conflict : Status::StorageUnavailable;
}
} // namespace hostlink::management::publication_runtime
