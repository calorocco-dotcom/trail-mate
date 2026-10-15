#include "platform/ui/navigation_runtime.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/map_tiles/sd_tmap_storage.h"
#include "ui_map_runtime/navigation/route_calculation.h"
#include "ui_map_runtime/navigation/route_kml_writer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <new>

namespace platform::ui::navigation
{
namespace
{
namespace sd = esp::arduino_common::storage;
constexpr const char* kDirectory = "/routes/navigation";
class Memory final : public tmap::MemoryProvider
{
  public:
    bool hasExternalMemory() const override { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) != 0; }
    void* allocate(size_t bytes, size_t alignment, tmap::MemoryRegion region) override
    {
        if (region != tmap::MemoryRegion::External) return nullptr;
        return heap_caps_aligned_alloc(alignment, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    void release(void* pointer, size_t, size_t, tmap::MemoryRegion) override { heap_caps_free(pointer); }
};
struct State
{
    Request request{};
    Snapshot snapshot{};
    Memory memory;
    esp::map_tiles::SdTmapStorage maps{true};
    ::navigation::RouteCalculation calculation{memory, tmap::MemoryPolicy::RequireExternal};
    ::navigation::RouteKmlWriter writer;
    sd::SdRuntimeFile output{sd::SdRuntimeMemoryPolicy::PsramOnly};
    std::atomic<bool> cancelled{false};
    bool running = true;
    uint32_t media = 0;
    char package[192]{}, pending[160]{}, saved[160]{};
};
State* s_state = nullptr;
SemaphoreHandle_t s_mutex = nullptr;
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
    return !state.cancelled && state.media == sd::sd_media_session() && sd::sd_card_ready();
}
void publish(State& state, Status status)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state.snapshot.status = status;
    ++state.snapshot.revision;
    xSemaphoreGive(s_mutex);
}
bool write(void* context, const char* bytes, size_t count)
{
    auto& state = *static_cast<State*>(context);
    size_t offset = 0;
    const auto started = state.maps.nowMs();
    while (offset < count && alive(state))
    {
        const auto written = state.output.write(bytes + offset, count - offset);
        if (written > count - offset) return false;
        offset += written;
        if (offset == count) return true;
        if (!state.output.read_busy() || state.maps.nowMs() - started >= 5000) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}
bool point(void* context, tmap::GeoPoint value)
{
    return static_cast<State*>(context)->writer.point(value);
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
    auto status = Status::NoRoutingData;
    bool active = false;
    uint32_t busy_since = state.maps.nowMs();
    while (alive(state))
    {
        if (!state.package[0])
        {
            const auto next = state.maps.nextPackage(state.package, sizeof(state.package));
            if (next == tmap::Status::Missing) break;
            if (next == tmap::Status::More || next == tmap::Status::Busy)
            {
                vTaskDelay(1);
                continue;
            }
            if (next != tmap::Status::Ok)
            {
                status = Status::StorageUnavailable;
                break;
            }
        }
        if (!active)
        {
            const auto opened = state.maps.openPackage(state.package);
            if (opened == tmap::Status::Busy || opened == tmap::Status::More)
            {
                if (state.maps.nowMs() - busy_since >= 30000)
                {
                    status = Status::StorageUnavailable;
                    break;
                }
                vTaskDelay(1);
                continue;
            }
            if (opened != tmap::Status::Ok)
            {
                status = Status::StorageUnavailable;
                break;
            }
            const auto result = state.calculation.begin(state.maps,
                                                        {state.request.origin.latitude_e7, state.request.origin.longitude_e7},
                                                        {state.request.destination.latitude_e7, state.request.destination.longitude_e7}, tmap::TravelMode::Foot, {});
            if (!::navigation::RouteCalculation::pending(result))
            {
                status = failure(result);
                break;
            }
            active = true;
        }
        const auto result = state.calculation.step(16);
        if (::navigation::RouteCalculation::pending(result))
        {
            if (result == tmap::Result::Busy && state.maps.nowMs() - busy_since >= 30000)
            {
                status = Status::StorageUnavailable;
                break;
            }
            if (result != tmap::Result::Busy) busy_since = state.maps.nowMs();
            vTaskDelay(1);
            continue;
        }
        if (result != tmap::Result::Ok)
        {
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
        const double distance = state.calculation.costUnits() / 100.0;
        if (!sd::sd_mkdir("/routes", state.media) || !sd::sd_mkdir(kDirectory, state.media) ||
            !state.output.open(state.pending, "w", state.media) ||
            !state.writer.begin(write, &state, state.request.origin.name,
                                {state.request.origin.latitude_e7, state.request.origin.longitude_e7}, state.request.destination.name,
                                {state.request.destination.latitude_e7, state.request.destination.longitude_e7}))
        {
            status = Status::StorageUnavailable;
            break;
        }
        auto geometry = state.calculation.beginGeometry(point, &state);
        while (alive(state) && ::navigation::RouteCalculation::pending(geometry))
        {
            geometry = state.calculation.step(4);
            vTaskDelay(1);
        }
        if (!alive(state))
        {
            status = Status::Cancelled;
            break;
        }
        if (geometry != tmap::Result::Ok || !state.writer.finish() || !state.output.flush())
        {
            status = Status::StorageUnavailable;
            break;
        }
        state.output.close();
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        state.snapshot.distance_m = distance;
        state.snapshot.points = static_cast<uint32_t>(state.writer.pointCount());
        std::strcpy(state.snapshot.path, state.pending);
        xSemaphoreGive(s_mutex);
        status = Status::Ready;
        break;
    }
    state.output.close();
    state.calculation.reset();
    state.maps.closePackage();
    state.maps.resetEnumeration();
    if (!alive(state)) status = state.cancelled ? Status::Cancelled : Status::StorageUnavailable;
    if (status != Status::Ready && sd::sd_exists(state.pending)) sd::sd_remove(state.pending, state.media);
    publish(state, status);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state.running = false;
    if (state.cancelled)
    {
        s_state = nullptr;
        xSemaphoreGive(s_mutex);
        if (sd::sd_exists(state.pending)) sd::sd_remove(state.pending, state.media);
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
    if (!supported() || !valid(request.origin) || !valid(request.destination) || !sd::sd_card_ready()) return false;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex || xSemaphoreTake(s_mutex, 0) != pdTRUE) return false;
    if (s_state)
    {
        xSemaphoreGive(s_mutex);
        return false;
    }
    void* memory = heap_caps_aligned_alloc(alignof(State), sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory)
    {
        xSemaphoreGive(s_mutex);
        return false;
    }
    auto* state = s_state = new (memory) State{};
    state->request = request;
    state->media = sd::sd_media_session();
    const auto id = static_cast<unsigned long>(esp_random());
    std::snprintf(state->pending, sizeof(state->pending), "%s/.pending-%08lx.kml", kDirectory, id);
    char origin[25], destination[25];
    endpointFilename(origin, sizeof(origin), request.origin.name);
    endpointFilename(destination, sizeof(destination), request.destination.name);
    std::snprintf(state->saved, sizeof(state->saved), "%s/nav-%s-%s-%08lx.kml", kDirectory, origin, destination, id);
    state->snapshot.status = Status::Calculating;
    state->snapshot.revision = 1;
    const bool started = xTaskCreate(worker, "navigation", 4096, state, 1, nullptr) == pdPASS;
    if (!started)
    {
        destroy(state);
        s_state = nullptr;
    }
    xSemaphoreGive(s_mutex);
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
    if (!s_mutex || xSemaphoreTake(s_mutex, 0) != pdTRUE) return false;
    const bool saved = s_state && !s_state->running && s_state->snapshot.status == Status::Ready &&
                       s_state->media == sd::sd_media_session() && !sd::sd_exists(s_state->saved) &&
                       sd::sd_rename(s_state->pending, s_state->saved, s_state->media);
    if (saved)
    {
        s_state->snapshot.status = Status::Saved;
        std::strcpy(s_state->snapshot.path, s_state->saved);
        ++s_state->snapshot.revision;
    }
    xSemaphoreGive(s_mutex);
    return saved;
}
void cancel()
{
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    auto* state = s_state;
    if (state && state->running) state->cancelled = true;
    else if (state)
    {
        if (state->snapshot.status != Status::Saved && sd::sd_exists(state->pending)) sd::sd_remove(state->pending, state->media);
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
        if (!is_directory && name[0] != '.' && length > 4 && !std::strcmp(name + length - 4, ".kml")) names.emplace_back(name);
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
