#include "platform/ui/navigation_runtime.h"
#include "platform/ui/navigation_trace.h"
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/map_tiles/sd_tmap_storage.h"
#include "platform/esp/arduino_common/navigation/sd_route_file.h"
#include "ui_map_runtime/navigation/archive_calculation.h"
#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <new>
#if defined(ARDUINO) && defined(TRAIL_MATE_ENABLE_USB_CDC_TRANSPORT)
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#endif

namespace platform::ui::navigation
{
void trace(const char* format, ...)
{
    va_list args, console;
    va_start(args, format);
    va_copy(console, args);
    std::vprintf(format, console);
    va_end(console);
#if defined(ARDUINO) && defined(TRAIL_MATE_ENABLE_USB_CDC_TRANSPORT)
    // Keep CDC protocol framing in HostLink's existing single-writer queue.
    // One external-only scratch owner, no route-sized or task-stack buffer.
    static std::atomic_flag occupied = ATOMIC_FLAG_INIT;
    static char* buffer = nullptr;
    constexpr size_t capacity = 384;
    if (hostlink::get_status().state == hostlink::LinkState::Ready &&
        !occupied.test_and_set(std::memory_order_acquire))
    {
        if (!buffer)
            buffer = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffer)
        {
            const auto length = std::vsnprintf(buffer, capacity, format, args);
            if (length > 0)
                hostlink::enqueue_event(0x83, reinterpret_cast<const uint8_t*>(buffer),
                                        std::min(static_cast<size_t>(length), capacity - 1));
        }
        occupied.clear(std::memory_order_release);
    }
#endif
    va_end(args);
}
namespace
{
namespace sd = esp::arduino_common::storage;
constexpr const char* kDirectory = "/routes/navigation";
void traceMemory(uint32_t request, const char* event)
{
    trace("[Navigation][memory] request=%lu event=%s psram_free=%u psram_largest=%u internal_free=%u internal_largest=%u sd_ready=%u usb_owner=%u media=%lu\n",
          static_cast<unsigned long>(request), event,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          unsigned(sd::sd_card_ready()), unsigned(sd::sd_external_block_owner_active()),
          static_cast<unsigned long>(sd::sd_media_session()));
}
const char* statusName(Status status)
{
    switch (status)
    {
    case Status::Idle:
        return "idle";
    case Status::Calculating:
        return "calculating";
    case Status::Ready:
        return "ready";
    case Status::Saved:
        return "saved";
    case Status::NoRoutingData:
        return "no_routing_data";
    case Status::NoPath:
        return "no_path";
    case Status::OutsideCoverage:
        return "outside_coverage";
    case Status::MemoryUnavailable:
        return "memory_unavailable";
    case Status::StorageUnavailable:
        return "storage_unavailable";
    case Status::Cancelled:
        return "cancelled";
    }
    return "unknown";
}
class MapInput final : public tmap::RandomAccessFile
{
  public:
    explicit MapInput(tmap::RandomAccessFile& source) : source_(source) {}
    uint64_t size() const override { return source_.size(); }
    tmap::Status readAt(uint64_t offset, uint8_t* bytes, size_t count) override
    {
        const auto result = source_.readAt(offset, bytes, count);
        if (result != tmap::Status::Ok && result != tmap::Status::Busy && result != tmap::Status::More)
            trace("[Navigation][storage_io] adapter=map operation=read offset=%" PRIu64 " requested=%u status=%u file_bytes=%" PRIu64 "\n",
                  offset, static_cast<unsigned>(count), unsigned(result), source_.size());
        return result;
    }

  private:
    tmap::RandomAccessFile& source_;
};
class Memory final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) != 0; }
    void* allocate(size_t bytes, size_t alignment, tmap::MemoryRegion region) override
    {
        if (region != tmap::MemoryRegion::External) return nullptr;
        auto* result = heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!result)
            trace("[Navigation][allocation] rejected bytes=%u alignment=%u psram_free=%u psram_largest=%u\n",
                  static_cast<unsigned>(bytes), static_cast<unsigned>(alignment),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        return result;
    }
    void release(void* pointer, size_t, size_t, tmap::MemoryRegion) override { heap_caps_free(pointer); }
};
struct State
{
    Request request{};
    Snapshot snapshot{};
    Memory memory;
    esp::map_tiles::SdTmapStorage maps{true};
    MapInput map_input{maps};
    ::navigation::ArchiveCalculation calculation{memory, tmap::MemoryPolicy::RequireExternal};
    esp::navigation::SdRouteFile output, spool;
    std::atomic<bool> cancelled{false};
    bool running = true, output_owned = false, spool_owned = false;
    uint32_t media = 0, cancel_generation = 0, request_id = 0;
    char package[192]{}, pending[160]{}, saved[160]{}, spool_path[160]{};
};
State* s_state = nullptr;
SemaphoreHandle_t s_mutex = nullptr;
std::atomic<uint32_t> s_cancel_generation{1};
std::atomic<uint32_t> s_request_sequence{0};
void endpointFilename(char* output, size_t capacity, const char* name)
{
    size_t length = std::min(std::strlen(name), capacity - 1);
    // Truncate at a complete UTF-8 boundary, including non-Latin POI names.
    while (length && (static_cast<unsigned char>(name[length]) & 0xc0) == 0x80) --length;
    for (size_t index = 0; index < length; ++index)
    {
        const auto byte = static_cast<unsigned char>(name[index]);
        output[index] = byte < 32 || std::strchr("/\\:*?\"<>|", byte) ? '_' : name[index];
    }
    while (length && (output[length - 1] == '.' || output[length - 1] == ' ')) --length;
    output[length] = 0;
    if (!length) std::snprintf(output, capacity, "place");
}
void destroy(State* state)
{
    if (!state) return;
    state->~State();
    heap_caps_free(state);
}
bool valid(const Endpoint& endpoint)
{
    return endpoint.latitude_e7 >= -900000000 && endpoint.latitude_e7 <= 900000000 &&
           endpoint.longitude_e7 >= -1800000000 && endpoint.longitude_e7 <= 1800000000 &&
           endpoint.name[0] && std::memchr(endpoint.name, 0, sizeof(endpoint.name));
}
bool alive(const State& state)
{
    return !state.cancelled && state.cancel_generation == s_cancel_generation.load() &&
           state.media == sd::sd_media_session() && sd::sd_card_ready();
}
void publish(State& state, Status status)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state.snapshot.status = status;
    ++state.snapshot.revision;
    xSemaphoreGive(s_mutex);
}
Status failure(tmap::Result result)
{
    switch (result)
    {
    case tmap::Result::NoPath:
        return Status::NoPath;
    case tmap::Result::OutsideCoverage:
    case tmap::Result::Incomplete:
        return Status::OutsideCoverage;
    case tmap::Result::BudgetExceeded:
        return Status::MemoryUnavailable;
    case tmap::Result::Missing:
    case tmap::Result::Unsupported:
    case tmap::Result::Invalid:
        return Status::NoRoutingData;
    case tmap::Result::Cancelled:
        return Status::Cancelled;
    default:
        return Status::StorageUnavailable;
    }
}
void worker(void* context)
{
    auto& state = *static_cast<State*>(context);
    const uint32_t started_at = state.maps.nowMs();
    uint32_t progress_at = started_at;
    trace("[Navigation][worker] request=%lu event=start media=%lu\n",
          static_cast<unsigned long>(state.request_id), static_cast<unsigned long>(state.media));
    auto status = Status::NoRoutingData;
    bool active = false;
    uint32_t busy_since = state.maps.nowMs();
    while (alive(state))
    {
        if (state.maps.nowMs() - progress_at >= 5000)
        {
            progress_at = state.maps.nowMs();
            trace("[Navigation][progress] request=%lu elapsed_ms=%lu phase=%u reserved_bytes=%u\n",
                  static_cast<unsigned long>(state.request_id), static_cast<unsigned long>(progress_at - started_at),
                  unsigned(state.calculation.phase()), static_cast<unsigned>(state.calculation.reservedBytes()));
        }
        if (!state.output_owned)
        {
            if ((!sd::sd_is_directory("/routes") && !sd::sd_mkdir("/routes", state.media)) ||
                (!sd::sd_is_directory(kDirectory) && !sd::sd_mkdir(kDirectory, state.media)))
            {
                trace("[Navigation][storage] request=%lu reason=mkdir_failed\n", static_cast<unsigned long>(state.request_id));
                traceMemory(state.request_id, "mkdir_failed");
                status = Status::StorageUnavailable;
                break;
            }
            state.output_owned = state.output.openNew(state.pending, state.media);
            if (!state.output_owned)
            {
                if (state.output.busy() && state.maps.nowMs() - busy_since < 30000)
                {
                    vTaskDelay(1);
                    continue;
                }
                status = Status::StorageUnavailable;
                trace("[Navigation][storage] request=%lu reason=pending_open_failed busy=%u\n",
                      static_cast<unsigned long>(state.request_id), unsigned(state.output.busy()));
                break;
            }
        }
        if (!state.spool_owned)
        {
            state.spool_owned = state.spool.openNew(state.spool_path, state.media);
            if (!state.spool_owned)
            {
                if (state.spool.busy() && state.maps.nowMs() - busy_since < 30000)
                {
                    vTaskDelay(1);
                    continue;
                }
                trace("[Navigation][storage] request=%lu reason=spool_open_failed busy=%u\n",
                      static_cast<unsigned long>(state.request_id), unsigned(state.spool.busy()));
                status = Status::StorageUnavailable;
                break;
            }
        }
        if (!state.package[0])
        {
            const auto next = state.maps.nextPackage(state.package, sizeof(state.package));
            if (next == tmap::Status::Missing)
            {
                trace("[Navigation][map] request=%lu event=enumeration_end final_status=%s\n",
                      static_cast<unsigned long>(state.request_id), statusName(status));
                break;
            }
            if (next == tmap::Status::More || next == tmap::Status::Busy)
            {
                vTaskDelay(1);
                continue;
            }
            if (next != tmap::Status::Ok)
            {
                trace("[Navigation][map] request=%lu reason=enumeration_failed io=%u\n",
                      static_cast<unsigned long>(state.request_id), unsigned(next));
                status = Status::StorageUnavailable;
                break;
            }
            trace("[Navigation][map] request=%lu event=candidate path=%s\n",
                  static_cast<unsigned long>(state.request_id), state.package);
        }
        if (!active)
        {
            const auto opened = state.maps.openPackage(state.package);
            if (opened == tmap::Status::Busy || opened == tmap::Status::More)
            {
                if (state.maps.nowMs() - busy_since >= 30000)
                {
                    trace("[Navigation][map] request=%lu reason=open_busy_timeout\n", static_cast<unsigned long>(state.request_id));
                    status = Status::StorageUnavailable;
                    break;
                }
                vTaskDelay(1);
                continue;
            }
            if (opened != tmap::Status::Ok)
            {
                trace("[Navigation][map] request=%lu reason=open_failed io=%u\n",
                      static_cast<unsigned long>(state.request_id), unsigned(opened));
                status = Status::StorageUnavailable;
                break;
            }
            const auto result = state.calculation.begin(state.map_input, state.spool, state.output,
                                                        {state.request.origin.latitude_e7, state.request.origin.longitude_e7},
                                                        {state.request.destination.latitude_e7, state.request.destination.longitude_e7}, {});
            trace("[Navigation][search] request=%lu event=begin result=%s file_bytes=%" PRIu64 " states=65536 search_array_budget=2097152\n",
                  static_cast<unsigned long>(state.request_id), tmap::resultName(result),
                  state.maps.size());
            if (!::navigation::ArchiveCalculation::pending(result))
            {
                status = failure(result);
                break;
            }
            active = true;
        }
        const auto previous_phase = state.calculation.phase();
        const auto result = state.calculation.step(256);
        if (previous_phase != state.calculation.phase())
            trace("[Navigation][phase] request=%lu from=%u to=%u result=%s reserved_bytes=%u\n",
                  static_cast<unsigned long>(state.request_id), unsigned(previous_phase), unsigned(state.calculation.phase()),
                  tmap::resultName(result), static_cast<unsigned>(state.calculation.reservedBytes()));
        if (::navigation::ArchiveCalculation::pending(result))
        {
            if (result == tmap::Result::Busy && state.maps.nowMs() - busy_since >= 30000)
            {
                trace("[Navigation][search] request=%lu reason=busy_timeout\n", static_cast<unsigned long>(state.request_id));
                status = Status::StorageUnavailable;
                break;
            }
            if (result != tmap::Result::Busy) busy_since = state.maps.nowMs();
            vTaskDelay(1);
            continue;
        }
        if (result != tmap::Result::Ok)
        {
            trace("[Navigation][search] request=%lu event=failed result=%s phase=%u\n",
                  static_cast<unsigned long>(state.request_id), tmap::resultName(result), unsigned(state.calculation.phase()));
            traceMemory(state.request_id, "search_failed");
            const auto reason = failure(result);
            if (reason == Status::StorageUnavailable || reason == Status::MemoryUnavailable)
            {
                status = reason;
                break;
            }
            if (reason != Status::NoRoutingData) status = reason;
            state.calculation.reset();
            state.maps.closePackage();
            state.package[0] = 0;
            active = false;
            busy_since = state.maps.nowMs();
            continue;
        }
        trace("[Navigation][export] request=%lu event=begin legs=%lu\n",
              static_cast<unsigned long>(state.request_id), static_cast<unsigned long>(state.calculation.legCount()));
        auto geometry = state.calculation.beginExport();
        while (alive(state) && ::navigation::ArchiveCalculation::pending(geometry))
        {
            geometry = state.calculation.step(4);
            if (state.maps.nowMs() - progress_at >= 5000)
            {
                progress_at = state.maps.nowMs();
                trace("[Navigation][export] request=%lu event=progress elapsed_ms=%lu result=%s points=%" PRIu64 "\n",
                      static_cast<unsigned long>(state.request_id), static_cast<unsigned long>(progress_at - started_at),
                      tmap::resultName(geometry), state.calculation.pointCount());
            }
            vTaskDelay(1);
        }
        if (!alive(state))
        {
            status = Status::Cancelled;
            break;
        }
        const bool flushed = geometry == tmap::Result::Ok && state.output.flush();
        if (!flushed)
        {
            trace("[Navigation][export] request=%lu event=failed result=%s flush_ok=%u\n",
                  static_cast<unsigned long>(state.request_id), tmap::resultName(geometry), unsigned(flushed));
            status = Status::StorageUnavailable;
            break;
        }
        state.output.close();
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        state.snapshot.distance_m = state.calculation.lengthCm() / 100.0;
        state.snapshot.points = static_cast<uint32_t>(state.calculation.pointCount());
        std::strcpy(state.snapshot.path, state.pending);
        xSemaphoreGive(s_mutex);
        status = Status::Ready;
        break;
    }
    state.output.close();
    state.spool.close();
    if (state.spool_owned) sd::sd_remove(state.spool_path, state.media);
    state.calculation.reset();
    state.maps.closePackage();
    state.maps.resetEnumeration();
    if (!alive(state)) status = state.cancelled || state.cancel_generation != s_cancel_generation.load() ? Status::Cancelled : Status::StorageUnavailable;
    if (state.output_owned && status != Status::Ready) sd::sd_remove(state.pending, state.media);
    trace("[Navigation][worker] request=%lu event=finish status=%s elapsed_ms=%lu points=%lu cancel=%u media_match=%u\n",
          static_cast<unsigned long>(state.request_id), statusName(status), static_cast<unsigned long>(state.maps.nowMs() - started_at),
          static_cast<unsigned long>(state.snapshot.points), unsigned(state.cancelled.load()), unsigned(state.media == sd::sd_media_session()));
    traceMemory(state.request_id, "worker_finished");
    publish(state, status);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state.running = false;
    if (state.cancelled || state.cancel_generation != s_cancel_generation.load())
    {
        s_state = nullptr;
        xSemaphoreGive(s_mutex);
        if (state.output_owned) sd::sd_remove(state.pending, state.media);
        destroy(&state);
    }
    else xSemaphoreGive(s_mutex);
    vTaskDelete(nullptr);
}
} // namespace
bool supported() { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) != 0; }
const char* directory() { return kDirectory; }
bool submit(const Request& request)
{
    const uint32_t request_id = s_request_sequence.fetch_add(1) + 1;
    const bool available = supported(), origin_valid = valid(request.origin), destination_valid = valid(request.destination);
    trace("[Navigation][submit] request=%lu event=begin trace=1 supported=%u origin_valid=%u destination_valid=%u\n",
          static_cast<unsigned long>(request_id), unsigned(available), unsigned(origin_valid), unsigned(destination_valid));
    const auto rejected = [request_id](const char* reason)
    {
        trace("[Navigation][submit] request=%lu event=rejected reason=%s\n", static_cast<unsigned long>(request_id), reason);
        traceMemory(request_id, reason);
        return false;
    };
    if (!available) return rejected("psram_unavailable");
    if (!origin_valid) return rejected("invalid_origin");
    if (!destination_valid) return rejected("invalid_destination");
    if (!sd::sd_card_ready()) return rejected("sd_not_ready");
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return rejected("mutex_allocation_failed");
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return rejected("mutex_busy");
    if (s_state)
    {
        trace("[Navigation][submit] request=%lu previous_request=%lu running=%u cancelled=%u status=%s\n",
              static_cast<unsigned long>(request_id), static_cast<unsigned long>(s_state->request_id),
              unsigned(s_state->running), unsigned(s_state->cancelled.load()), statusName(s_state->snapshot.status));
        xSemaphoreGive(s_mutex);
        return rejected("previous_job_retained");
    }
    void* memory = heap_caps_aligned_alloc(alignof(State), sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
    {
        xSemaphoreGive(s_mutex);
        return rejected("state_allocation_failed");
    }
    auto* state = s_state = new (memory) State{};
    state->request_id = request_id;
    state->request = request;
    state->media = sd::sd_media_session();
    state->cancel_generation = s_cancel_generation.load();
    const auto id = static_cast<unsigned long>(esp_random());
    std::snprintf(state->pending, sizeof(state->pending), "%s/.pending-%08lx.tmroute", kDirectory, id);
    std::snprintf(state->spool_path, sizeof(state->spool_path), "%s/.spool-%08lx", kDirectory, id);
    char origin[25], destination[25];
    endpointFilename(origin, sizeof(origin), request.origin.name);
    endpointFilename(destination, sizeof(destination), request.destination.name);
    std::snprintf(state->saved, sizeof(state->saved), "%s/nav-%s-%s-%08lx.tmroute", kDirectory, origin, destination, id);
    state->snapshot.status = Status::Calculating;
    state->snapshot.revision = 1;
    traceMemory(request_id, "before_task_create");
    const auto task_result = xTaskCreate(worker, "navigation", 4096, state, 1, nullptr);
    const bool started = task_result == pdPASS;
    trace("[Navigation][submit] request=%lu event=task_create result=%ld state_bytes=%u stack_request_bytes=4096\n",
          static_cast<unsigned long>(request_id), static_cast<long>(task_result), static_cast<unsigned>(sizeof(State)));
    if (!started)
    {
        destroy(state);
        s_state = nullptr;
    }
    xSemaphoreGive(s_mutex);
    if (!started) return rejected("task_create_failed");
    return started;
}
bool poll(Snapshot& snapshot)
{
    if (!s_mutex || xSemaphoreTake(s_mutex, 0) != pdTRUE) return false;
    const bool changed = s_state && snapshot.revision != s_state->snapshot.revision;
    if (changed) snapshot = s_state->snapshot;
    xSemaphoreGive(s_mutex);
    return changed;
}
bool save()
{
    if (!s_mutex || xSemaphoreTake(s_mutex, 0) != pdTRUE)
    {
        trace("[Navigation][save] event=rejected reason=mutex_unavailable\n");
        return false;
    }
    const bool saved = s_state && !s_state->running && s_state->snapshot.status == Status::Ready &&
                       s_state->media == sd::sd_media_session() && !sd::sd_exists(s_state->saved) &&
                       sd::sd_rename(s_state->pending, s_state->saved, s_state->media);
    if (saved)
    {
        s_state->snapshot.status = Status::Saved;
        std::strcpy(s_state->snapshot.path, s_state->saved);
        ++s_state->snapshot.revision;
    }
    trace("[Navigation][save] request=%lu ok=%u running=%u status=%s media_match=%u\n",
          static_cast<unsigned long>(s_state ? s_state->request_id : 0), unsigned(saved), unsigned(s_state && s_state->running),
          s_state ? statusName(s_state->snapshot.status) : "no_job", unsigned(s_state && s_state->media == sd::sd_media_session()));
    xSemaphoreGive(s_mutex);
    return saved;
}
void cancel()
{
    // Signal before taking the UI mutex. Even if a durable save temporarily
    // owns it, the worker observes cancellation and releases its own resources.
    const auto generation = s_cancel_generation.fetch_add(1) + 1;
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        trace("[Navigation][cancel] generation=%lu event=signal_only mutex_present=%u\n",
              static_cast<unsigned long>(generation), unsigned(s_mutex != nullptr));
        return;
    }
    auto* state = s_state;
    trace("[Navigation][cancel] generation=%lu request=%lu running=%u status=%s\n",
          static_cast<unsigned long>(generation), static_cast<unsigned long>(state ? state->request_id : 0),
          unsigned(state && state->running), state ? statusName(state->snapshot.status) : "no_job");
    if (state && state->running) state->cancelled = true;
    else if (state)
    {
        if (state->output_owned && state->snapshot.status != Status::Saved) sd::sd_remove(state->pending, state->media);
        s_state = nullptr;
        destroy(state);
    }
    xSemaphoreGive(s_mutex);
}
bool list(std::vector<std::string>& names, size_t maximum)
{
    names.clear();
    sd::SdRuntimeDir files(sd::SdRuntimeMemoryPolicy::PsramOnly);
    const auto opened = files.open_read_status(kDirectory, sd::sd_media_session());
    if (opened == sd::SdFileReadStatus::Missing) return true;
    if (opened != sd::SdFileReadStatus::Ready) return false;
    char name[128];
    while (names.size() < maximum)
    {
        bool is_directory = false;
        const auto next = files.read_next_status(name, sizeof(name), &is_directory);
        if (next == sd::SdDirReadStatus::End) return true;
        if (next != sd::SdDirReadStatus::Entry) return false;
        const auto length = std::strlen(name);
        const bool kml = length > 4 && !std::strcmp(name + length - 4, ".kml");
        const bool archive = length > 8 && !std::strcmp(name + length - 8, ".tmroute");
        if (!is_directory && name[0] != '.' && (kml || archive)) names.emplace_back(name);
    }
    return true;
}
bool remove(const char* name)
{
    if (!name || !name[0] || name[0] == '.' || std::strchr(name, '/') || std::strchr(name, '\\') || std::strlen(name) >= 128) return false;
    char path[160];
    std::snprintf(path, sizeof(path), "%s/%s", kDirectory, name);
    return sd::sd_remove(path, sd::sd_media_session());
}
} // namespace platform::ui::navigation
#endif
