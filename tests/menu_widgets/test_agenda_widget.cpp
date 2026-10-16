#include "agenda/usecase/agenda_service.h"
#include "esp_heap_caps.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/storage/agenda_file_store.h"
#include "platform/ui/agenda_widget_runtime.h"
#include "platform/ui/time_runtime.h"
#include "ui_presentation/agenda/agenda_source.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
namespace
{
unsigned allocations = 0;
bool fail_allocation = false;
} // namespace
void* heap_caps_malloc(std::size_t bytes, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_allocation) return nullptr;
    auto* result = std::malloc(bytes);
    if (result) ++allocations;
    return result;
}
void heap_caps_free(void* memory)
{
    if (memory)
    {
        assert(allocations);
        --allocations;
    }
    std::free(memory);
}
namespace platform::ui::time
{
time_t apply_timezone_offset_for_utc(time_t utc) { return utc; }
int timezone_profile_id() { return 0; }
int timezone_offset_min() { return 0; }
} // namespace platform::ui::time
int main()
{
    using agenda::StoreResult;
    using namespace agenda_test;
    fail_allocation = true;
    assert(!platform::ui::create_agenda_widget_source());
    fail_allocation = false;
    auto* source = platform::ui::create_agenda_widget_source();
    assert(source && allocations == 1 && opens == 0 && reads == 0);
    const auto now = source->currentTime();
    assert(now.valid && opens == 0);
    ui::agenda::AgendaRequest request;
    request.day_start = now.calendar_seconds / 86400 * 86400;
    ui::agenda::AgendaSnapshot snapshot;
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::Ok && snapshot.page.count == 0 && handles == 0);
    // Generate the fixture with the real store/service, without duplicating its codec.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string path = "agenda-widget-" + std::to_string(stamp) + ".dat", temporary = path + ".init";
    platform::esp::storage::AgendaFileStore store(path.c_str(), temporary.c_str());
    assert(store.begin() == StoreResult::Ok);
    agenda::AgendaService service(store);
    agenda::EventRecord event;
    event.start_time = request.day_start + 3600;
    std::strcpy(event.title, "Trail meeting");
    uint32_t id = 0;
    assert(service.create(event, id) == agenda::AgendaResult::Ok);
    {
        std::ifstream file(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(file), {});
    }
    assert(std::remove(path.c_str()) == 0);
    const auto original = bytes;
    present = true;
    ++session;
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::Ok && snapshot.page.count == 1);
    assert(!std::strcmp(snapshot.page.rows[0].title, "Trail meeting"));
    assert(handles == 0 && bytes == original);
    agenda::EventRecord detail;
    assert(source->detail(id, detail) == agenda::AgendaResult::Ok && detail.id == id);
    ui::agenda::AgendaEditorModel draft;
    assert(!source->newDraft(draft));
    external = true;
    const auto before = opens;
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::IoError && !snapshot.storage_ready && opens == before);
    external = false;
    ready = false;
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::IoError && snapshot.page.count == 0 && opens == before);
    ready = true;
    fail_open = true;
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::IoError); // An I/O failure must not appear empty.
    fail_open = false;
    bytes[0] ^= 1;
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::Corrupt && handles == 0);
    bytes = original;
    change_session_at_read = reads + 2; // Header succeeds, card changes during slot scan.
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::IoError && snapshot.page.count == 0 && !snapshot.storage_ready && handles == 0);
    change_session_at_read = 0;
    source->snapshot(request, snapshot);
    assert(snapshot.result == StoreResult::Ok && snapshot.page.count == 1 && bytes == original);
    platform::ui::destroy_agenda_widget_source(source);
    assert(allocations == 0 && handles == 0);
    std::puts("P4 read-only Agenda: PSRAM lifetime, actual records, missing/corrupt files, USB ownership, removal and card changes passed.");
}
